/* homtom-status-screen: dashboard state, parser for /tmp/homtom-ui.txt and the drawing/touch logic.
 * Hardware independent; built into both the device binary and the host preview. */
#ifndef UI_H
#define UI_H
#include "gui.h"

#define UI_PAGES 3
#define UI_NAV_Y 216
#define UI_NAV_H 48
#define UI_SLIDER_X 24
#define UI_SLIDER_Y 160
#define UI_SLIDER_W 360
#define UI_SLIDER_H 40

typedef struct {
	int have;		/* a file was read */
	int batt;		/* battery_pct_est, -1 unknown */
	int chg, ext;		/* charging, external_power */
	int light;		/* light_raw (10-bit ADC), -1 unknown */
	int fixq, sats;		/* from gga: fix quality, satellites used */
	int hdop10, alt10;	/* tenths */
	int rmc_ok, spd10, crs10;	/* rmc status A, speed in tenths of km/h, course tenths of a degree */
	int hh, mm, ss;		/* UTC from gga/rmc, -1 unknown */
	u32 up;			/* uptime_s */
	int page;		/* 0 home, 1 gps, 2 system */
	int press;		/* nav button held, -1 none */
	int slider, drag;	/* demo slider 0..100, finger is dragging it */
} ui_state;

void ui_init(ui_state *s);
void ui_parse(ui_state *s, const char *buf, int len);	/* key=value lines; unknown keys ignored, missing keys keep "unknown" */
void ui_draw(const ui_state *s);			/* draws the whole screen into the gui target */
int ui_touch(ui_state *s, int x, int y, int down);	/* screen coordinates; returns 1 when a redraw is needed */

#endif
