/* cfg: configurable UI for homtom-gui-cfg. Parses ui.conf (docs/UI_CONFIG.md), draws its pages with gui.c, maps touches to
 * widgets and runs actions through a fixed allow-list. Hardware independent: the platform supplies plat_spawn/plat_event.
 * No libc, no libgcc: divisions by variables go through gui_udiv, no struct copies (they can emit memcpy). */
#ifndef CFG_H
#define CFG_H
#include "ui.h"

#define CFG_PATH "/mnt/internal/homtom/ui.conf"
#define CFG_EVENTS "/tmp/ui.events"
#define CFG_HA_PATH "/tmp/ha.states"
#define CFG_FILE_MAX 12288
#define CFG_LINE_MAX 200
#define CFG_PAGES 20		/* at most CFG_NAV of them visible (area pages); the rest hidden=1 subpages and hub=1 */
#define CFG_NAV 6
#define CFG_WIDGETS 96
#define CFG_POOL 6144
#define CFG_VARS 12
#define CFG_KEYS 40
#define CFG_KPOOL 2048
#define CFG_ERR 64

/* Home Assistant states file (docs/HA_UI.md) and timing of touch and HA actions */
#define CFG_HA_MAX 4096		/* bytes read; a line cut at the end is ignored */
#define CFG_HA_KEYS 64
#define CFG_HA_LINE 80
#define CFG_HA_STALE 60		/* seconds: older (or missing) file = offline look */
#define CFG_HA_OPT 8		/* optimistic values shown until confirmed or expired */
#define CFG_OPT_MS 5000
#define CFG_HOLD_MS 600		/* long press: held this long ... */
#define CFG_HOLD_PX 12		/* ... without moving further than this */
#define CFG_THROTTLE_MS 300	/* drag sends at most every 300 ms */
#define CFG_IDLE_MS 1000	/* thermostat target is sent after 1 s without a change */
#define CFG_HINT_MS 15000	/* a hold hint disappears after this long without the key being released */
#define CFG_SWIPE_PX 60		/* horizontal travel of a page swipe (and more than twice the vertical travel) */

#define NAV_TABS 1		/* ui nav=: page names as tabs in the header (up to 5 area pages) */
#define NAV_DOTS 2		/* ui nav=: page dots */

enum { W_NONE, W_LABEL, W_TILE, W_BAR, W_GAUGE, W_BATTERY, W_BUTTON, W_SLIDER, W_TOGGLE, W_LIST, W_ROW, W_ON, W_ENTITY,
       W_LIGHTCTL, W_THERMO };
#define W_MAXTYPE W_THERMO
enum { A_NONE, A_PAGE, A_BACKLIGHT, A_VOLUME, A_MUTE, A_BEEP, A_CMD, A_EVENT, A_STORAGE, A_RELOAD, A_SET, A_HA, A_SCREEN, A_TZ };
enum { F_TEXT, F_INT, F_TENTHS, F_YESNO, F_ONOFF, F_PCT, F_UPTIME };
enum { SLOT_BACKLIGHT, SLOT_AUDIO, SLOT_CMD, SLOT_REBOOT, SLOT_GPS, SLOT_BT, SLOT_N };	/* one child per slot */

#define WF_LOW 1	/* low= given */
#define WF_GOOD 2	/* good= given */
#define WF_CONFIRM 4	/* confirm=1 */
#define WF_ARG 8	/* arg= given */
#define WF_XY 16	/* explicit x/y/w/h instead of the page grid */
#define WF_HIGH 32	/* high= given */
#define WF_RANGE 64	/* min= or max= given */
#define WF_GBELOW 128	/* good_below= given */
#define WF_NA 256	/* unavailable_below= given */

#define CAP_SET 1	/* lightctl: ct_min/ct_max/color/dim given, so they (not the states keys) decide the controls */
#define CAP_CT 2
#define CAP_HUE 4
#define CAP_NODIM 8

typedef struct {
	u8 type, page, fmt, action, font, color, align;
	u16 flags;
	short x, y, w, h;
	short var;			/* index into the runtime var table, -1 none */
	u16 label, key, unit, arg, sub, subunit, colorkey, vname, verb;	/* string pool offsets, 0 = empty; verb: row button text */
	u16 slot, harg, modes;		/* HA slot ("*" = the slot that opened the page), hold= argument, thermo modes= */
	u8 hold, icon, caps;		/* hold= action (A_PAGE/A_HA, 0 none), entity icon + 1 (0 none), lightctl CAP_* */
	u16 detail;			/* lightctl detail= page id */
	int min, max, step, low, good, high, ctmin, ctmax, gbelow, nabelow;	/* thermo: step in tenths */
	short line;			/* config line, for error messages */
	u8 col, row, cs, rs;		/* grid cell and span (at=, span=); used when x/y/w/h are not given */
} cfg_widget;

typedef struct {
	u16 id, title, group, parent;	/* group: hidden pages of one group are swipe neighbours; parent: target of Back */
	u8 cols, rows, hidden, hub;	/* hub: the System hub (gear, power key long), not swiped */
} cfg_page;

typedef struct {
	int ok;				/* parsed without error */
	int npages, nwidgets, nav, start, nvis, uses_ha, wrap;	/* nav: NAV_TABS | NAV_DOTS; nvis: area pages */
	int theme, tz, tzeu, hub, autolock;	/* autolock: seconds without touch or key, 0 off */	/* theme 1 light; tz minutes east of UTC, tzeu EU summer time; hub page or -1 */
	u16 title;
	cfg_page page[CFG_PAGES];
	cfg_widget w[CFG_WIDGETS];
	int nvdecl;
	u16 vdecl[CFG_VARS];		/* var name= value= declarations */
	int vinit[CFG_VARS];
	char pool[CFG_POOL];
	int pool_used;
	int err_line;
	char err[CFG_ERR];
} cfg_conf;

typedef struct {
	char name[16];
	int value;
} cfg_var;

typedef struct {
	char k[24], v[16];		/* "SLOT.FIELD" (k[0] = 0: free), value shown until confirmed or `until` */
	u32 until;
} cfg_opt;

/* press/drag codes: widget index, plus part << 8 for the parts of lightctl/thermo; nav and the confirm dialog above */
#define P_NAV 0x10000
#define P_CANCEL 0x20000
#define P_OK 0x20001
#define P_BACK 0x30000	/* header: back arrow, gear, then the dot strip ends */
#define P_GEAR 0x30001
#define P_PREV 0x30002
#define P_NEXT 0x30003

typedef struct {
	cfg_conf *c;			/* active config; the built-in one (conf/default.conf, compiled in) when builtin */
	int builtin;
	ui_state st;			/* status values parsed by ui_parse */
	int nkeys, kpool_used;
	u16 kname[CFG_KEYS], kval[CFG_KEYS];
	char kpool[CFG_KPOOL];
	cfg_var var[CFG_VARS];
	int nvars;
	int page, press, drag, confirm;	/* press/drag: P_* code or widget (| part << 8); confirm: widget awaiting OK */
	u32 seq;
	char note[CFG_ERR];		/* header message (reload error, refused action) */
	int note_ttl;			/* redraws left for the note */
	int reload;			/* set by the reload action; the platform re-reads the file */
	u32 now;			/* monotonic ms, set by the platform before cfg_touch/cfg_tick */
	u32 wall;			/* UTC seconds since 1970 for the header clock, 0 unknown (set by the platform) */
	int area, lcmore;
	char tzset[16];			/* INTERNAL:/homtom/tz (auto, MIN, MIN,eu, MIN,us); empty = missing */
	int locked;			/* screen locked: backlight off, touches ignored, no redraws */
	int ovl, ovl_lit;		/* power key hold hint: 5 or 10 (key hold5/hold10), 0 none; backlight lit for it while locked */
	u32 ovl_t;			/* now when the hint appeared */
	u32 last_act;			/* now of the last touch or key (autolock) */		/* last area page shown (Back from the hub); light page shows the hue strip */
	/* Home Assistant: /tmp/ha.states without the "ha." prefix, optimistic overlay, pending send */
	int hn, hpool_used, hage;	/* hage: file age in s, -1 missing */
	u16 hname[CFG_HA_KEYS], hval[CFG_HA_KEYS];
	char hpool[CFG_HA_MAX + 4];
	cfg_opt opt[CFG_HA_OPT];
	char dslot[16];			/* slot of the widget that opened the current page (slot=*) */
	int prev;			/* page before the last page change (arg=back) */
	int hcand, px0, py0, swallow;	/* long press candidate, where it started; swallow touches until the finger lifts */
	int down, sx, sy, lx, ly, noswipe;	/* finger down: where the gesture started, last position, started on a control */
	u32 t0, last_send, due;
	int pend;			/* pslot/pop wait for `due` */
	char pslot[16], pop[24], dop[24], lastop[24];
} cfg_ctx;

/* platform hooks (hw_cfg.c on the device, host.c for the preview) */
int plat_spawn(int slot, const char *const *argv);	/* start argv[0] with argv, no shell; 0 started, <0 busy/failed */
void plat_event(const char *line, int len);		/* append one line to /tmp/ui.events */

void cfg_init(cfg_ctx *x);
int cfg_parse(cfg_conf *c, const char *buf, int len);	/* 1 ok; 0 error (c->err_line, c->err) */
void cfg_use(cfg_ctx *x, cfg_conf *c);			/* activate (NULL = built-in config); keeps var values */
void cfg_status(cfg_ctx *x, const char *buf, int len);	/* /tmp/homtom-ui.txt contents */
void cfg_ha(cfg_ctx *x, const char *buf, int len, int age);	/* /tmp/ha.states contents (len < 0: missing), age in s */
void cfg_draw(cfg_ctx *x);
int cfg_touch(cfg_ctx *x, int px, int py, int down);	/* 1 when a redraw is needed */
int cfg_tick(cfg_ctx *x);				/* timers (long press, pending sends, expiry); 1 = redraw */
int cfg_wait(const cfg_ctx *x);				/* ms until cfg_tick has something to do (max 1000) */
int cfg_key(cfg_ctx *x, const char *name);		/* "short"/"long" power key; 1 when a redraw is needed */
void cfg_lock(cfg_ctx *x, int on);			/* screen lock: backlight 0 and back, event=screen */
void cfg_emit(cfg_ctx *x, const char *ev, const char *name, int val);	/* one line to /tmp/ui.events */
void cfg_set_note(cfg_ctx *x, const char *a, const char *b);
void cfg_go(cfg_ctx *x, int page);			/* show a page (remembers the previous one for arg=back) */
int cfg_page_count(const cfg_ctx *x);
const char *cfg_page_id(const cfg_ctx *x, int i);
u32 cfg_hash(const char *buf, int len);
int cfg_tz_offset(int base, int rule, u32 utc);		/* minutes; rule 0 none, 1 EU, 2 US summer time */
void cfg_tzfile(cfg_ctx *x, const char *buf, int len);	/* INTERNAL:/homtom/tz contents (len < 0: missing) */
int plat_tz_write(const char *val);			/* write the tz file atomically; 0 ok */

#endif
