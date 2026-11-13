#include <stdlib.h>
#include <string.h>
#include "layout.h"

#define TILE_W ((SCREEN_W - (COLS + 1) * GAP) / COLS)
#define TILE_H ((SCREEN_H - BAR_H - ROWS * GAP) / ROWS)
#define CORNER_W 120
#define SWIPE_MIN 70
#define DOT_STEP 14

void layout_tile(int slot, struct rect *r)
{
	int col = slot % COLS, row = slot / COLS;
	r->x = GAP + col * (TILE_W + GAP);
	r->y = BAR_H + row * (TILE_H + GAP);
	r->w = col == COLS - 1 ? SCREEN_W - GAP - r->x : TILE_W;
	r->h = TILE_H;
}

void layout_page_dots(int npages, struct rect *r)
{
	r->w = npages * DOT_STEP + 16;
	r->x = (SCREEN_W - r->w) / 2;
	r->y = 0;
	r->h = BAR_H;
}

int layout_hit(int x, int y)
{
	struct rect r;
	int slot;
	if (y < BAR_H) {
		if (x < CORNER_W)
			return HIT_CORNER;
		if (x >= SCREEN_W / 2 - 80 && x < SCREEN_W / 2 + 80)
			return HIT_PAGES;
		return HIT_BAR;
	}
	for (slot = 0; slot < COLS * ROWS; slot++) {
		layout_tile(slot, &r);
		if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
			return slot;
	}
	return HIT_NONE;
}

/* Climate tiles: the outer thirds step the target, the middle toggles. */
int layout_zone(const struct rect *r, int x)
{
	if (x < r->x + r->w / 3)
		return -1;
	if (x >= r->x + r->w - r->w / 3)
		return 1;
	return 0;
}

int layout_gesture(int x0, int y0, int x1, int y1)
{
	int dx = x1 - x0, dy = abs(y1 - y0);
	if (abs(dx) >= SWIPE_MIN && dy < abs(dx) / 2)
		return dx < 0 ? G_LEFT : G_RIGHT;
	if (abs(dx) < 30 && dy < 30)
		return G_TAP;
	return G_NONE;
}

int rect_overlap(const struct rect *a, const struct rect *b)
{
	return a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
}

int calib_parse(struct calib *c, const char *text)
{
	char *end;
	int i;
	for (i = 0; i < 7; i++) {
		c->a[i] = strtol(text, &end, 10);
		if (end == text)
			return -1;
		text = end;
	}
	return c->a[6] ? 0 : -1;
}

void calib_apply(const struct calib *c, int rx, int ry, int *x, int *y)
{
	long tx = (c->a[2] + c->a[0] * rx + c->a[1] * ry) / c->a[6];
	long ty = (c->a[5] + c->a[3] * rx + c->a[4] * ry) / c->a[6];
	*x = tx < 0 ? 0 : tx >= SCREEN_W ? SCREEN_W - 1 : (int)tx;
	*y = ty < 0 ? 0 : ty >= SCREEN_H ? SCREEN_H - 1 : (int)ty;
}

#define EV_SYN 0
#define EV_KEY 1
#define EV_ABS 3
#define BTN_TOUCH 0x14a
#define ABS_X 0
#define ABS_Y 1
#define ABS_PRESSURE 0x18

/* The GO 630 driver (drivers/barcelona/tsinput) reports pressure 1023 while down and 0 on
 * release, never BTN_TOUCH; the emulator bridge sends both. Either one counts. */
int touch_feed(struct touch *t, int type, int code, int value)
{
	int pressed;
	if (type == EV_ABS) {
		if (code == ABS_X)
			t->rx = value;
		else if (code == ABS_Y)
			t->ry = value;
		else if (code == ABS_PRESSURE)
			t->pressure = value;
		return T_NONE;
	}
	if (type == EV_KEY && code == BTN_TOUCH) {
		t->btn = value ? 1 : -1;
		return T_NONE;
	}
	if (type != EV_SYN || code != 0)
		return T_NONE;
	pressed = t->btn ? t->btn > 0 : t->pressure > 0;
	if (t->btn < 0 || t->pressure == 0)
		pressed = 0;
	if (t->btn < 0)
		t->btn = 0;
	calib_apply(&t->cal, t->rx, t->ry, &t->x, &t->y);
	if (pressed && !t->down) {
		t->down = 1;
		return T_PRESS;
	}
	if (!pressed && t->down) {
		t->down = 0;
		return T_RELEASE;
	}
	return pressed ? T_MOVE : T_NONE;
}
