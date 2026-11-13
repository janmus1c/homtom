/* RGB565 drawing with integer maths only: the ARM920T has no FPU. */
#include <string.h>
#include "gfx.h"

static unsigned short blend(unsigned short d, unsigned short s, int a)
{
	int r, g, b;
	if (a >= 16)
		return s;
	if (a <= 0)
		return d;
	r = ((d >> 11) * (16 - a) + (s >> 11) * a) >> 4;
	g = (((d >> 5) & 63) * (16 - a) + ((s >> 5) & 63) * a) >> 4;
	b = ((d & 31) * (16 - a) + (s & 31) * a) >> 4;
	return (unsigned short)((r << 11) | (g << 5) | b);
}

static void plot(struct canvas *c, int x, int y, unsigned short col, int a)
{
	if (x >= 0 && y >= 0 && x < c->w && y < c->h)
		c->px[y * c->w + x] = blend(c->px[y * c->w + x], col, a);
}

void gfx_fill(struct canvas *c, int x, int y, int w, int h, unsigned short col)
{
	int i, j;
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > c->w) w = c->w - x;
	if (y + h > c->h) h = c->h - y;
	for (j = 0; j < h; j++) {
		unsigned short *row = c->px + (y + j) * c->w + x;
		for (i = 0; i < w; i++)
			row[i] = col;
	}
}

/* Coverage in 1/16 of pixel (px, py) by a disc; centre and radius in 1/8 px, 4x4 samples. */
static int disc_cover(int px, int py, int cx, int cy, int r)
{
	int sx, sy, n = 0, dx, dy;
	for (sy = 0; sy < 4; sy++)
		for (sx = 0; sx < 4; sx++) {
			dx = px * 8 + sx * 2 + 1 - cx;
			dy = py * 8 + sy * 2 + 1 - cy;
			if (dx * dx + dy * dy <= r * r)
				n++;
		}
	return n;
}

void gfx_rrect(struct canvas *c, int x, int y, int w, int h, int r, unsigned short col)
{
	int i, j, a;
	if (r * 2 > h) r = h / 2;
	if (r * 2 > w) r = w / 2;
	gfx_fill(c, x + r, y, w - 2 * r, h, col);
	gfx_fill(c, x, y + r, r, h - 2 * r, col);
	gfx_fill(c, x + w - r, y + r, r, h - 2 * r, col);
	for (j = 0; j < r; j++)
		for (i = 0; i < r; i++) {
			a = disc_cover(i, j, r * 8, r * 8, r * 8);
			plot(c, x + i, y + j, col, a);
			plot(c, x + w - 1 - i, y + j, col, a);
			plot(c, x + i, y + h - 1 - j, col, a);
			plot(c, x + w - 1 - i, y + h - 1 - j, col, a);
		}
}

void gfx_circle(struct canvas *c, int cx, int cy, int r, unsigned short col)
{
	gfx_ring(c, cx, cy, r, r, col);
}

/* Ring of thickness t (t >= r fills the disc); the centre is the corner between four pixels. */
void gfx_ring(struct canvas *c, int cx, int cy, int r, int t, unsigned short col)
{
	int i, j, a, inner = r - t;
	for (j = -r; j < r; j++)
		for (i = -r; i < r; i++) {
			a = disc_cover(i, j, 0, 0, r * 8);
			if (inner > 0)
				a -= disc_cover(i, j, 0, 0, inner * 8);
			if (a > 0)
				plot(c, cx + i, cy + j, col, a);
		}
}

void gfx_alpha(struct canvas *c, int x, int y, int w, int h, const unsigned char *bits, unsigned short col)
{
	int i, j, stride = (w + 1) / 2, a;
	for (j = 0; j < h; j++)
		for (i = 0; i < w; i++) {
			a = bits[j * stride + i / 2];
			a = i & 1 ? a & 15 : a >> 4;
			if (a)
				plot(c, x + i, y + j, col, a == 15 ? 16 : a);
		}
}

/* UTF-8 to a code point in 32..255; anything else becomes '?'. */
static int next_char(const char **s)
{
	const unsigned char *p = (const unsigned char *)*s;
	int cp;
	if (p[0] < 0x80) {
		cp = p[0];
		*s += 1;
	} else if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
		cp = ((p[0] & 31) << 6) | (p[1] & 63);
		*s += 2;
	} else {
		cp = '?';
		*s += 1;
		while ((**s & 0xC0) == 0x80)
			*s += 1;
	}
	return cp < 32 || cp > 255 ? '?' : cp;
}

static const struct glyph *glyph_of(const struct font *f, int cp)
{
	const struct glyph *g = &f->glyphs[cp - 32];
	return g->adv ? g : &f->glyphs['?' - 32];
}

int gfx_text_width(const struct font *f, const char *s, int track)
{
	int w = 0;
	while (*s)
		w += glyph_of(f, next_char(&s))->adv + track;
	return (w + 8) >> 4;
}

int gfx_text(struct canvas *c, const struct font *f, int x, int base, const char *s,
	     unsigned short col, int track, int max_w)
{
	int pen = x << 4, limit = (x + max_w) << 4, dots = 0;
	const struct glyph *dot = glyph_of(f, '.');
	const char *p;

	if (max_w > 0 && gfx_text_width(f, s, track) > max_w)
		dots = 3 * (dot->adv + track);
	while (*s) {
		const struct glyph *g;
		p = s;
		g = glyph_of(f, next_char(&p));
		if (dots && pen + g->adv + track + dots > limit) {
			int k;
			for (k = 0; k < 3; k++) {
				gfx_alpha(c, (pen >> 4) + dot->bx, base - dot->by, dot->w, dot->h, f->bits + dot->off, col);
				pen += dot->adv + track;
			}
			break;
		}
		if (g->w)
			gfx_alpha(c, ((pen + 8) >> 4) + g->bx, base - g->by, g->w, g->h, f->bits + g->off, col);
		pen += g->adv + track;
		s = p;
	}
	return (pen >> 4) - x;
}
