#ifndef GFX_H
#define GFX_H

#include "assets.h"

#define RGB(r, g, b) ((unsigned short)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

struct canvas {
	unsigned short *px;
	int w, h;
};

void gfx_fill(struct canvas *c, int x, int y, int w, int h, unsigned short col);
void gfx_rrect(struct canvas *c, int x, int y, int w, int h, int r, unsigned short col);
void gfx_circle(struct canvas *c, int cx, int cy, int r, unsigned short col);
void gfx_ring(struct canvas *c, int cx, int cy, int r, int t, unsigned short col);
void gfx_alpha(struct canvas *c, int x, int y, int w, int h, const unsigned char *bits, unsigned short col);
int gfx_text_width(const struct font *f, const char *s, int track);
/* Draws at the baseline; text wider than max_w ends in an ellipsis. Returns the width drawn. */
int gfx_text(struct canvas *c, const struct font *f, int x, int base, const char *s,
	     unsigned short col, int track, int max_w);

#endif
