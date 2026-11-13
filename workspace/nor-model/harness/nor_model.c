#include "nor_model.h"

const uint32_t nm_sector_base[NM_NSECT] = {
	0x00000, 0x02000, 0x03000, 0x04000, 0x08000, 0x10000, 0x18000, 0x20000, 0x28000, 0x30000, 0x38000
};
const uint32_t nm_sector_len[NM_NSECT] = {
	0x2000, 0x1000, 0x1000, 0x4000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000
};

static uint32_t rnd16(struct nor_model *c)
{
	c->rng ^= c->rng << 13;
	c->rng ^= c->rng >> 7;
	c->rng ^= c->rng << 17;
	return (uint32_t)(c->rng >> 24) & 0xffff;
}

int nm_sector_of(uint32_t word)
{
	int i;

	word %= NM_WORDS;
	for (i = 0; i < NM_NSECT; i++)
		if (word >= nm_sector_base[i] && word < nm_sector_base[i] + nm_sector_len[i])
			return i;
	return -1;
}

void nm_init(struct nor_model *c, const uint8_t *image)
{
	uint32_t i;

	for (i = 0; i < NM_WORDS; i++)
		c->cells[i] = image ? (uint16_t)(image[2 * i] | image[2 * i + 1] << 8) : 0xffff;
	c->protected_mask = 1u << 0;
	c->silent_protect_mask = 0;
	c->access_us = 1.0;
	c->program_us = 8.0;
	c->program_max_us = 300.0;
	c->erase_us = 500000.0;
	c->now = c->busy_until = 0.0;
	c->id0 = 0x007f;
	c->id100 = 0x001c;
	c->id1 = 0x22ba;
	c->state = c->after_unlock = NM_READ;
	c->toggle = 0;
	c->prog_word = 0;
	c->prog_data = 0;
	c->prog_effect = c->prog_fail = 0;
	c->erase_sectors = c->erase_targets = 0;
	c->stuck_busy = 0;
	c->fail_next_program = 0;
	c->slow_next_program_us = 0;
	c->f0_resets_setup = 0;
	c->ignored_while_busy = c->f0_while_busy = 0;
	c->rng = 0x9e3779b97f4a7c15ull;
}

void nm_image(const struct nor_model *c, uint8_t *out)
{
	uint32_t i;

	for (i = 0; i < NM_WORDS; i++) {
		out[2 * i] = (uint8_t)c->cells[i];
		out[2 * i + 1] = (uint8_t)(c->cells[i] >> 8);
	}
}

static void finish(struct nor_model *c)
{
	int s;
	uint32_t w;

	if (c->state == NM_BUSY_PROG) {
		if (c->prog_effect)
			c->cells[c->prog_word] &= c->prog_data;
		if (c->prog_effect && c->prog_fail) {
			c->state = NM_FAILED;
			return;
		}
	} else {
		for (s = 0; s < NM_NSECT; s++)
			if (c->erase_targets & (1u << s))
				for (w = nm_sector_base[s]; w < nm_sector_base[s] + nm_sector_len[s]; w++)
					c->cells[w] = 0xffff;
	}
	c->state = NM_READ;
	c->after_unlock = NM_READ;
}

static void advance(struct nor_model *c)
{
	if ((c->state == NM_BUSY_PROG || c->state == NM_BUSY_ERASE) && !c->stuck_busy && c->now >= c->busy_until)
		finish(c);
}

void nm_wait(struct nor_model *c, double us)
{
	c->now += us;
	advance(c);
}

static uint16_t autoselect(struct nor_model *c, uint32_t word)
{
	uint32_t off = word & 0xff;

	if (off == 0)
		return (word & 0x100) ? c->id100 : c->id0;
	if (off == 1)
		return c->id1;
	if (off == 2)
		return (c->protected_mask >> nm_sector_of(word)) & 1;
	return 0;
}

static uint16_t status(struct nor_model *c, uint32_t word)
{
	unsigned v;

	c->toggle ^= 0x40;
	if (c->state == NM_BUSY_PROG || c->state == NM_FAILED) {
		v = ((~c->prog_data) & 0x80) | c->toggle | (c->state == NM_FAILED ? 0x20 : 0);
	} else {
		v = c->toggle | 0x08;
		if (((c->erase_sectors >> nm_sector_of(word)) & 1) && (c->toggle & 0x40))
			v |= 0x04;
	}
	return (uint16_t)(v | v << 8);
}

uint16_t nm_read(struct nor_model *c, uint32_t word)
{
	c->now += c->access_us;
	advance(c);
	word %= NM_WORDS;
	if (c->state == NM_BUSY_PROG || c->state == NM_BUSY_ERASE || c->state == NM_FAILED)
		return status(c, word);
	if (c->state == NM_AUTOSEL || ((c->state == NM_CYC1 || c->state == NM_CYC2) && c->after_unlock == NM_AUTOSEL))
		return autoselect(c, word);
	return c->cells[word];
}

static void start_program(struct nor_model *c, uint32_t word, uint16_t data)
{
	unsigned refuse = c->protected_mask | c->silent_protect_mask;

	c->prog_word = word;
	c->prog_data = data;
	c->toggle = 0;
	c->state = NM_BUSY_PROG;
	if ((refuse >> nm_sector_of(word)) & 1) {
		c->busy_until = c->now + 2.0;
		c->prog_fail = 0;
		c->prog_effect = 0;
		return;
	}
	c->prog_effect = 1;
	c->prog_fail = (data & ~c->cells[word] & 0xffff) != 0;
	c->busy_until = c->now + (c->prog_fail ? c->program_max_us : c->program_us);
	if (c->fail_next_program) {
		c->fail_next_program = 0;
		c->prog_fail = 1;
		c->prog_data = (uint16_t)(c->cells[word] & (data | (rnd16(c) & ~data)));	/* only part of it clears */
		c->busy_until = c->now + c->program_max_us;
	}
	if (c->slow_next_program_us > 0) {
		c->busy_until = c->now + c->slow_next_program_us;
		c->slow_next_program_us = 0;
	}
}

static void start_erase(struct nor_model *c)
{
	unsigned refuse = c->protected_mask | c->silent_protect_mask;
	int s, n = 0;

	c->erase_targets = c->erase_sectors & ~refuse;
	for (s = 0; s < NM_NSECT; s++)
		n += (c->erase_targets >> s) & 1;
	c->state = NM_BUSY_ERASE;
	c->toggle = 0;
	c->busy_until = c->now + (n ? c->erase_us * n : 100.0);
}

void nm_write(struct nor_model *c, uint32_t word, uint16_t data)
{
	uint32_t low;
	unsigned d;
	int st = c->state;

	c->now += c->access_us;
	advance(c);
	st = c->state;
	word %= NM_WORDS;
	low = word & 0x7ff;
	d = data & 0xff;
	if (st == NM_BUSY_PROG || st == NM_BUSY_ERASE) {
		c->ignored_while_busy++;
		if (d == 0xf0)
			c->f0_while_busy++;
		return;
	}
	if (st == NM_FAILED) {
		if (d == 0xf0)
			c->state = NM_READ;
		return;
	}
	if (st == NM_PROG_SETUP && !(c->f0_resets_setup && d == 0xf0)) {
		start_program(c, word, data);
		return;
	}
	if (d == 0xf0) {
		c->state = NM_READ;
		c->after_unlock = NM_READ;
		return;
	}
	if ((st == NM_READ || st == NM_AUTOSEL) && low == 0x555 && d == 0xaa) {
		c->after_unlock = st;
		c->state = NM_CYC1;
		return;
	}
	if (st == NM_CYC1 && low == 0x2aa && d == 0x55) {
		c->state = NM_CYC2;
		return;
	}
	if (st == NM_CYC2 && low == 0x555) {
		if (d == 0x90) {
			c->state = NM_AUTOSEL;
			return;
		}
		if (d == 0xa0) {
			c->state = NM_PROG_SETUP;
			return;
		}
		if (d == 0x80) {
			c->state = NM_ERASE_CYC3;
			return;
		}
	}
	if (st == NM_ERASE_CYC3 && low == 0x555 && d == 0xaa) {
		c->state = NM_ERASE_CYC4;
		return;
	}
	if (st == NM_ERASE_CYC4 && low == 0x2aa && d == 0x55) {
		c->state = NM_ERASE_CYC5;
		return;
	}
	if (st == NM_ERASE_CYC5) {
		if (low == 0x555 && d == 0x10) {
			c->erase_sectors = (1u << NM_NSECT) - 1;
			start_erase(c);
			return;
		}
		if (d == 0x30) {
			c->erase_sectors = 1u << nm_sector_of(word);
			start_erase(c);
			return;
		}
	}
	if (st == NM_READ && low == 0x055 && d == 0x98)
		return;
	if (st == NM_CYC1 || st == NM_CYC2)
		c->state = c->after_unlock;
	else
		c->state = st == NM_AUTOSEL ? NM_AUTOSEL : NM_READ;
	if (c->state == NM_READ)
		c->after_unlock = NM_READ;
}

void nm_power_loss(struct nor_model *c)
{
	int s;
	uint32_t w;

	if (c->state == NM_BUSY_PROG && c->prog_effect) {
		uint16_t clearing = c->cells[c->prog_word] & ~c->prog_data;
		uint16_t mask = (uint16_t)(rnd16(c) & clearing);

		c->cells[c->prog_word] &= (uint16_t)~mask;
	} else if (c->state == NM_BUSY_ERASE) {
		for (s = 0; s < NM_NSECT; s++)
			if (c->erase_targets & (1u << s))
				for (w = nm_sector_base[s]; w < nm_sector_base[s] + nm_sector_len[s]; w++)
					c->cells[w] = (uint16_t)rnd16(c);
	}
	c->state = NM_READ;
	c->after_unlock = NM_READ;
	c->stuck_busy = 0;
}
