#ifndef LAYOUT_H
#define LAYOUT_H

#define SCREEN_W 480
#define SCREEN_H 272
#define BAR_H 34
#define GAP 8
#define COLS 3
#define ROWS 2
#define LONG_PRESS_MS 2000

struct rect {
	int x, y, w, h;
};

enum { HIT_NONE = -1, HIT_CORNER = -2, HIT_PAGES = -3, HIT_BAR = -4 };
enum { G_NONE, G_TAP, G_LEFT, G_RIGHT };

void layout_tile(int slot, struct rect *r);
int layout_hit(int x, int y);
int layout_zone(const struct rect *r, int x);
int layout_gesture(int x0, int y0, int x1, int y1);
void layout_page_dots(int npages, struct rect *r);
int rect_overlap(const struct rect *a, const struct rect *b);

/* tslib pointercal: x = (a2 + a0*rx + a1*ry) / a6, y = (a5 + a3*rx + a4*ry) / a6 */
struct calib {
	long a[7];
};

int calib_parse(struct calib *c, const char *text);
void calib_apply(const struct calib *c, int rx, int ry, int *x, int *y);

/* Touch events from the evdev stream (ABS_X/ABS_Y/ABS_PRESSURE, EV_SYN commits a sample). */
enum { T_NONE, T_PRESS, T_MOVE, T_RELEASE };

struct touch {
	int rx, ry, pressure, btn, down;
	int x, y;
	struct calib cal;
};

int touch_feed(struct touch *t, int type, int code, int value);

#endif
