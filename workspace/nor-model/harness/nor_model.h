/* C port of ../en29lv400bb.py for the norwrite harness. Same states, same rules, same virtual clock; the
 * replay and fuzz checks in run_harness.py compare it read for read against the Python model.
 * Extra fault hooks that the Python model does not have are marked "harness only". */
#ifndef NOR_MODEL_H
#define NOR_MODEL_H

#include <stdint.h>

#define NM_WORDS 0x40000u
#define NM_NSECT 11

enum { NM_READ, NM_CYC1, NM_CYC2, NM_AUTOSEL, NM_PROG_SETUP, NM_ERASE_CYC3, NM_ERASE_CYC4, NM_ERASE_CYC5,
       NM_BUSY_PROG, NM_BUSY_ERASE, NM_FAILED };

struct nor_model {
	uint16_t cells[NM_WORDS];
	unsigned protected_mask;	/* sectors that refuse program/erase and report protect = 1 */
	unsigned silent_protect_mask;	/* harness only: refuse program/erase but report protect = 0 */
	double access_us, program_us, program_max_us, erase_us, now, busy_until;
	uint16_t id0, id100, id1;	/* autoselect words 0, 0x100, 1 */
	int state, after_unlock, toggle;
	uint32_t prog_word;
	uint16_t prog_data;
	int prog_effect, prog_fail;
	unsigned erase_sectors, erase_targets;
	int stuck_busy;
	int fail_next_program;	/* harness only: the next program ends in DQ5 after program_max_us */
	double slow_next_program_us;	/* harness only: the next program takes this long */
	int f0_resets_setup;	/* harness only: F0 after A0 resets instead of being program data */
	long ignored_while_busy;	/* writes the chip ignored because an embedded operation was running */
	long f0_while_busy;
	uint64_t rng;
};

extern const uint32_t nm_sector_base[NM_NSECT];
extern const uint32_t nm_sector_len[NM_NSECT];

void nm_init(struct nor_model *c, const uint8_t *image);
int nm_sector_of(uint32_t word);
uint16_t nm_read(struct nor_model *c, uint32_t word);
void nm_write(struct nor_model *c, uint32_t word, uint16_t data);
void nm_wait(struct nor_model *c, double us);
void nm_power_loss(struct nor_model *c);
void nm_image(const struct nor_model *c, uint8_t *out);

#endif
