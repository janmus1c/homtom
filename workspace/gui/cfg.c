/* cfg: ui.conf parser, renderer, touch handling and action allow-list. Format: docs/UI_CONFIG.md. */
#include "cfg.h"
#define GUI_ICON_ENUM_ONLY
#include "icon_data.h"

#define BL_MIN 5	/* backlight floor in percent: the UI can never switch the panel fully dark */
#define VOL_RAW_MIN 48	/* homtom-snd2 --vol: below 48 is mute, 117 is the stock level; never louder than stock */
#define VOL_RAW_MAX 117
#define X0 8
#define Y0 40
#define CW 464
#define GAP 8

/* ---------- small string helpers ---------- */

static int slen(const char *s)
{
	int n = 0;

	while (s[n])
		n++;
	return n;
}

static int seq(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

static int prefix(const char *s, const char *p)
{
	while (*p)
		if (*s++ != *p++)
			return 0;
	return 1;
}

static void scopy(char *d, const char *s, int max)
{
	int i;

	for (i = 0; i < max - 1 && s[i]; i++)
		d[i] = s[i];
	d[i] = 0;
}

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int is_digit(char c) { return c >= '0' && c <= '9'; }

/* "12.34" -> 123 tenths; 0 when not a number */
static int tenths(const char *s, int *out)
{
	int neg = 0, v = 0;

	if (*s == '-') {
		neg = 1;
		s++;
	}
	if (!is_digit(*s))
		return 0;
	while (is_digit(*s))
		v = v * 10 + (*s++ - '0');
	v *= 10;
	if (*s == '.' && is_digit(s[1]))
		v += s[1] - '0';
	*out = neg ? -v : v;
	return 1;
}

static int whole(const char *s, int *out)
{
	int t;

	if (!tenths(s, &t))
		return 0;
	*out = t < 0 ? -((-t + 5) / 10) : (t + 5) / 10;
	return 1;
}

static int truthy(const char *s)
{
	return s[0] == '1' || s[0] == 't' || s[0] == 'T' || s[0] == 'y' || s[0] == 'Y' || (s[0] == 'o' && s[1] == 'n');
}

static char *itoa_s(char *d, int v)
{
	if (v < 0) {
		*d++ = '-';
		v = -v;
	}
	return gui_utoa(d, (u32)v);
}

/* strict integer: optional '-', digits, nothing else */
static int pnum(const char *s, int *out)
{
	int neg = 0, v = 0;

	if (*s == '-') {
		neg = 1;
		s++;
	}
	if (!*s)
		return 0;
	for (; *s; s++) {
		if (!is_digit(*s) || v > 100000000)
			return 0;
		v = v * 10 + (*s - '0');
	}
	*out = neg ? -v : v;
	return 1;
}

/* a,b pair of small non-negative integers */
static int ppair(const char *s, int *a, int *b, char sep)
{
	int i = 0, va = 0, vb = 0;

	if (!is_digit(s[0]))
		return 0;
	while (is_digit(s[i]) && i < 4)
		va = va * 10 + (s[i++] - '0');
	if (s[i++] != sep || !is_digit(s[i]))
		return 0;
	while (is_digit(s[i]) && i < 9)
		vb = vb * 10 + (s[i++] - '0');
	if (s[i])
		return 0;
	*a = va;
	*b = vb;
	return 1;
}

/* identifiers: [a-z0-9_] plus '-' when dash (page ids, var and event names) or ',' when comma (modes= lists), 1..15
   chars per name */
static int name_ok(const char *s, int dash, int comma)
{
	int n = 0;

	for (; *s; s++, n++) {
		if (comma && *s == ',' && n && s[1]) {
			n = -1;
			continue;
		}
		if (!((*s >= 'a' && *s <= 'z') || is_digit(*s) || *s == '_' || (dash && *s == '-')) || n > 14)
			return 0;
	}
	return n > 0;
}

#define ident(s) name_ok(s, 1, 0)

u32 cfg_hash(const char *buf, int len)
{
	u32 h = 2166136261u;
	int i;

	for (i = 0; i < len; i++)
		h = (h ^ (u8)buf[i]) * 16777619u;
	return h ^ (u32)len;
}

/* ---------- command allow-list: the config names an entry, never a path or a shell line ---------- */

static const char *const cmd_name[] = { "gps-on", "gps-off", "screenshot", "gps-aid", "bt-visible", "mic-test", "restart",
					"power-off" };
/* commands that use the same hardware share a slot, so they never run at the same time (mic-test and beep share the codec);
   restart and power-off use the reboot slot: once its child exists nothing else starts */
static const u8 cmd_slot[] = { SLOT_GPS, SLOT_GPS, SLOT_CMD, SLOT_GPS, SLOT_BT, SLOT_AUDIO, SLOT_REBOOT, SLOT_REBOOT };
static const char *const cmd_argv[][5] = {
	{ "/bin/homtom-hw", "gps-on", 0 },
	{ "/bin/homtom-hw", "gps-off", 0 },
	{ "/tmp/homtom-shot", "-o", "/tmp/homtom-shot.hts", 0 },
	{ "/bin/homtom-gps-aid", 0 },
	{ "/bin/homtom-bt-visible", 0 },
	{ "/bin/homtom-mic-test", 0 },
	{ "/bin/reboot", 0 },
	{ "/bin/homtom-poweroff", 0 },
};
#define CMD_RESTART 6	/* these two always ask first, as action=storage */
#define CMD_POWEROFF 7
#define NCMD ((int)(sizeof(cmd_name) / sizeof(cmd_name[0])))

static int cmd_find(const char *s)
{
	int i;

	for (i = 0; i < NCMD; i++)
		if (seq(s, cmd_name[i]))
			return i;
	return -1;
}

/* a toggle with action=cmd arg=NAME runs NAME-on or NAME-off from the allow-list */
static int cmd_pair(const char *name, int on)
{
	char b[24];
	int n = slen(name);

	if (n > 15)
		return -1;
	scopy(b, name, 16);
	scopy(b + n, on ? "-on" : "-off", 5);
	return cmd_find(b);
}

/* ---------- parser ---------- */

static const char *const wtype_name[] = { "", "label", "tile", "bar", "gauge", "battery", "button", "slider", "toggle",
					  "list", "row", "on", "entity", "lightctl", "thermo" };
static const char *const act_name[] = { "none", "page", "backlight", "volume", "mute", "beep", "cmd", "event", "storage",
					"reload", "set", "ha", "screen", "tz" };
#define NACT 14
static const char *const gps_key[] = { "fix", "fixq", "sats", "utc", "alt", "speed", "course", "hdop" };
static const char *const nav_name[] = { "none", "tabs", "dots", "both", "0", "1" };
static const u8 nav_val[] = { 0, NAV_TABS, NAV_DOTS, NAV_TABS | NAV_DOTS, 0, NAV_TABS | NAV_DOTS };
/* icon=: these short names (older configs), or any mdi name compiled into icon_data.h (gen_assets.sh) */
static const char *const icon_name[] = { "bulb", "thermometer", "drop", "co2", "cloud", "person", "presence", "fire",
					 "heat", "snow", "ac", "power" };
static const u8 icon_id[] = { ICON_LIGHTBULB, ICON_THERMOMETER, ICON_WATER_PERCENT, ICON_MOLECULE_CO2, ICON_MOLECULE_CO2,
			      ICON_ACCOUNT, ICON_MOTION_SENSOR, ICON_FIRE, ICON_RADIATOR, ICON_SNOWFLAKE, ICON_AIR_CONDITIONER,
			      ICON_POWER };

/* widget keys with a string value: the u16 pool offset each one fills (label= and text= are the same) */
#define WO(f) ((u8)__builtin_offsetof(cfg_widget, f))
static const char *const skey_name[] = { "label", "text", "key", "unit", "sub", "charging", "subunit", "colorkey", "power",
					 "arg", "slot", "modes", "var", "verb", "detail" };
static const u8 skey_off[] = { WO(label), WO(label), WO(key), WO(unit), WO(sub), WO(sub), WO(subunit), WO(colorkey),
			       WO(colorkey), WO(arg), WO(slot), WO(modes), WO(vname), WO(verb), WO(detail) };
/* widget keys with a number: int fields, then the short pixel fields x/y/w/h (0..480), and the flag each one sets */
static const char *const nkey_name[] = { "min", "max", "step", "low", "good", "high", "good_below", "unavailable_below",
					 "x", "y", "w", "h" };
static const u8 nkey_off[] = { WO(min), WO(max), WO(step), WO(low), WO(good), WO(high), WO(gbelow), WO(nabelow), WO(x),
			       WO(y), WO(w), WO(h) };
static const u16 nkey_flag[] = { WF_RANGE, WF_RANGE, 0, WF_LOW, WF_GOOD, WF_HIGH, WF_GBELOW, WF_NA, WF_XY, WF_XY, WF_XY,
				 WF_XY };
#define NK_XY 8
static const char *const fmt_name[] = { "text", "int", "tenths", "yesno", "onoff", "pct", "uptime" };
static const char *const col_name[] = { "bone", "muted", "blue", "ok", "warn", "bad", "ink" };
/* color= as theme roles: ok is the normal text colour (colour is spent on problems, docs/UI_DESIGN.md) */
static const u8 col_role[] = { T_TEXT, T_TEXT2, T_PRIMARY, T_TEXT, T_WARNING, T_ERROR, T_BG };

static int lookup(const char *const *tab, int n, const char *s)
{
	int i;

	for (i = 0; i < n; i++)
		if (seq(s, tab[i]))
			return i;
	return -1;
}

static int fail(cfg_conf *c, int line, const char *a, const char *b)
{
	char *p = c->err;
	int n = 0;

	c->ok = 0;
	c->err_line = line;
	while (*a && n < CFG_ERR - 1)
		p[n++] = *a++;
	while (b && *b && n < CFG_ERR - 1)
		p[n++] = *b++;
	p[n] = 0;
	return 0;
}

static u16 pstr(cfg_conf *c, const char *s)
{
	int n = slen(s), o = c->pool_used;

	if (!n)
		return 0;
	if (o + n + 1 > CFG_POOL)
		return 0xFFFF;
	scopy(c->pool + o, s, n + 1);
	c->pool_used += n + 1;
	return (u16)o;
}

#define PS(c, o) ((c)->pool + (o))

/* "21" or "21.5" */
static int tt_ok(const char *s)
{
	int n = 0;

	while (is_digit(*s))
		s++, n++;
	if (*s == '.' && is_digit(s[1]))
		s += 2;
	return n && n < 4 && !*s;
}

/* HA operation of action=ha / tap=ha:OP (docs/HA_UI.md) */
static int ha_op_ok(const char *s)
{
	const char *v = s;
	int a, b;

	while (*v && *v != ':')
		v++;
	if (!*v++)
		return seq(s, "toggle") || seq(s, "press");
	if (prefix(s, "mode:"))
		return name_ok(v, 0, 0);
	if (prefix(s, "set_target:"))
		return tt_ok(v);
	if (prefix(s, "set_hue_sat:"))
		return ppair(v, &a, &b, ',') && a < 360 && b <= 100;
	if (!pnum(v, &a) || a < 0)
		return 0;
	return prefix(s, "set_bri:") ? a <= 100 : prefix(s, "set_ct:") && a >= 2000 && a <= 6500;
}

/* a time zone setting: auto, MIN, MIN,eu or MIN,us; MIN in -720..840. 1 when valid, *min and *rule (0 none, 1 eu, 2 us)
   filled in; *rule = -1 for auto */
static int tz_parse(const char *v, int *min, int *rule)
{
	char b[8];
	int i;

	*min = 0;
	*rule = -1;
	if (seq(v, "auto"))
		return 1;
	for (i = 0; v[i] && v[i] != ',' && i < 7; i++)
		b[i] = v[i];
	b[i] = 0;
	if (!pnum(b, min) || *min < -720 || *min > 840)
		return 0;
	v += i;
	*rule = !*v ? 0 : seq(v, ",eu") ? 1 : seq(v, ",us") ? 2 : -2;
	return *rule >= 0;
}

/* tz:set:SETTING or tz:step:MINUTES */
static int tz_arg_ok(const char *q)
{
	int a, b;

	if (prefix(q, "set:"))
		return tz_parse(q + 4, &a, &b);
	return prefix(q, "step:") && pnum(q + 5, &a) && a >= -120 && a <= 120;
}

static void wzero(cfg_widget *w)
{
	char *p = (char *)w;
	u32 i;

	for (i = 0; i < sizeof(*w); i++)
		p[i] = 0;
	w->var = -1;
	w->max = 100;
	w->step = 1;
	w->cs = w->rs = 1;
}

/* error detail of wkey and wcheck_msg (the bad value), appended to their message */
static const char *werr_arg;

/* one key=value of a widget line: 0 when fine, else the error message (werr_arg its detail) */
static const char *wkey(cfg_conf *c, cfg_widget *w, const char *k, char *v)
{
	int a, b, i;
	u16 o;

	if (seq(k, "tap") || seq(k, "hold")) {
		char *q = v;

		while (*q && *q != ':')
			q++;
		if (*q)
			*q++ = 0;
		i = lookup(act_name, NACT, v);
		if (!*q && (seq(v, "toggle") || seq(v, "press")))
			i = A_HA, q = v;	/* tap=toggle is tap=ha:toggle */
		if ((i != A_HA && i != A_PAGE && i != A_SCREEN && i != A_TZ) || !*q)
			return "tap=/hold= must be ha:OP or page:ID";
		if ((i == A_SCREEN && !seq(q, "lock") && !seq(q, "unlock") && !seq(q, "toggle")) || (i == A_TZ && !tz_arg_ok(q)))
			return werr_arg = q, "bad value ";
		o = pstr(c, q);
		if (o == 0xFFFF)
			return "string pool full";
		if (k[0] == 't') {
			w->action = (u8)i;
			w->arg = o;
			w->flags |= WF_ARG;
		} else {
			w->hold = (u8)i;
			w->harg = o;
		}
		return 0;
	}
	if (seq(k, "icon")) {
		if ((i = lookup(icon_name, 12, v)) >= 0)
			i = icon_id[i];
		else if ((i = gui_icon_find(prefix(v, "mdi:") ? v + 4 : v)) < 0)
			return werr_arg = v, "unknown icon ";
		w->icon = (u8)(i + 1);
		return 0;
	}

	if (seq(k, "at")) {
		if (!ppair(v, &a, &b, ','))
			return werr_arg = v, "bad at=";
		w->col = (u8)a;
		w->row = (u8)b;
		return 0;
	}
	if (seq(k, "span")) {
		if (!ppair(v, &a, &b, ',') || !a || !b)
			return werr_arg = v, "bad span=";
		w->cs = (u8)a;
		w->rs = (u8)b;
		return 0;
	}
	if (w->type == W_LIGHTCTL && (seq(k, "ct_min") || seq(k, "ct_max") || seq(k, "color") || seq(k, "dim"))) {
		/* what the light supports, as the Mac-side generator writes it (docs/HA_UI.md) */
		if (!pnum(v, &a) || (k[1] == 't' ? a < 2000 || a > 6500 : a < 0 || a > 1))
			return werr_arg = k, "bad value for ";
		w->caps |= CAP_SET | (k[1] == 't' ? CAP_CT : k[0] == 'c' ? a * CAP_HUE : !a * CAP_NODIM);
		if (k[1] == 't')
			*(k[4] == 'i' ? &w->ctmin : &w->ctmax) = a;
		return 0;
	}
	if (w->type == W_THERMO && seq(k, "step")) {
		if (!tt_ok(v) || !tenths(v, &a) || a < 1)
			return werr_arg = v, "bad number ";
		w->step = a;
		return 0;
	}
	if ((i = lookup(nkey_name, 12, k)) >= 0) {
		if (!pnum(v, &a) || (i >= NK_XY && (a < 0 || a > 480)))
			return werr_arg = v, "bad number ";
		if (i >= NK_XY)
			*(short *)((char *)w + nkey_off[i]) = (short)a;
		else
			*(int *)((char *)w + nkey_off[i]) = a;
		w->flags |= nkey_flag[i];
		return 0;
	}
	if (seq(k, "fmt")) {
		if ((i = lookup(fmt_name, 7, v)) < 0)
			return werr_arg = v, "unknown fmt ";
		w->fmt = (u8)i;
		return 0;
	}
	if (seq(k, "color")) {
		if ((i = lookup(col_name, 7, v)) < 0)
			return werr_arg = v, "unknown color ";
		w->color = (u8)i;
		return 0;
	}
	if (seq(k, "font")) {
		if (!(v[0] == 's' || v[0] == 'm' || v[0] == 'l') || v[1])
			return "font must be s, m or l";
		w->font = v[0] == 's' ? 1 : v[0] == 'l' ? 2 : 0;
		return 0;
	}
	if (seq(k, "align")) {
		if ((i = lookup((const char *const[]){ "left", "center", "right" }, 3, v)) < 0)
			return "align must be left, center or right";
		w->align = (u8)i;
		return 0;
	}
	if (seq(k, "action")) {
		if ((i = lookup(act_name, NACT, v)) < 0 || i == A_SCREEN || i == A_TZ)
			return werr_arg = v, "action not allowed: ";
		w->action = (u8)i;
		if (i == A_STORAGE)
			w->flags |= WF_CONFIRM;
		return 0;
	}
	if (seq(k, "confirm")) {
		if (truthy(v))
			w->flags |= WF_CONFIRM;
		return 0;
	}
	if ((i = lookup(skey_name, w->type == W_LIGHTCTL ? 15 : 14, k)) < 0)
		return werr_arg = k, "unknown key ";
	if (((i == 12 || i == 14) && !ident(v)) || (i == 10 && !seq(v, "*") && !name_ok(v, 0, 0)) || (i == 11 && !name_ok(v, 0, 1)))
		return werr_arg = k, "bad value for ";
	if (i == 2 && w->type == W_ON && !seq(v, "short") && !seq(v, "long"))
		return "on key= must be short or long";
	if (i == 9)
		w->flags |= WF_ARG;
	o = pstr(c, v);
	if (o == 0xFFFF)
		return "string pool full";
	*(u16 *)((char *)w + skey_off[i]) = o;
	return 0;
}

static int find_page(const cfg_conf *c, const char *id)
{
	int i;

	for (i = 0; i < c->npages; i++)
		if (seq(PS(c, c->page[i].id), id))
			return i;
	return -1;
}

/* swipe neighbours: from a visible page the visible pages, from a hidden page the hidden pages of its group */
static int peer(const cfg_conf *c, int cur, int a)
{
	const cfg_page *p = &c->page[cur], *q = &c->page[a];

	if (p->hub || q->hub)
		return 0;
	if (!p->hidden)
		return !q->hidden;
	return p->group && q->hidden && q->group && seq(PS(c, p->group), PS(c, q->group));
}

static int page_known(const cfg_conf *c, const char *a)
{
	return seq(a, "next") || seq(a, "prev") || seq(a, "back") || seq(a, "cycle") || find_page(c, a) >= 0;
}

/* checks that need the whole widget line: the error message (werr_arg its detail), 0 when fine */

static const char *wcheck_msg(cfg_conf *c, cfg_widget *w)
{
	int t = w->type, a = w->action;
	const char *arg = PS(c, w->arg);

	if (t == W_ROW) {
		int k = c->nwidgets - 1;

		if (k < 0 || (c->w[k].type != W_LIST && c->w[k].type != W_ROW) || c->w[k].page != w->page)
			return "row must follow a list or row";
		if (!w->key && !w->action)
			return "row needs key=";
	}
	if ((t == W_TILE || t == W_BAR || t == W_GAUGE) && !w->key)
		return "needs key=";
	if ((t == W_SLIDER || t == W_TOGGLE || (t == W_BUTTON && a == A_MUTE)) && !w->vname) {
		if (a == A_BACKLIGHT)
			w->vname = pstr(c, "backlight");
		else if (a == A_VOLUME)
			w->vname = pstr(c, "volume");
		else if (a == A_MUTE)
			w->vname = pstr(c, "mute");
		else
			return "needs var=";
		if (w->vname == 0xFFFF)
			return "string pool full";
	}
	if (t == W_SLIDER && (w->max <= w->min || w->step <= 0))
		return "slider needs min < max and step > 0";
	if ((t == W_BAR || t == W_GAUGE) && w->max <= w->min)
		return "needs min < max";
	if (t == W_ON && (!w->key || !a))
		return "on needs key= and action=";
	if ((t == W_LABEL || t == W_LIST || t == W_BATTERY || t >= W_LIGHTCTL || (t == W_ROW && a != A_PAGE && a != A_TZ)) &&
	    (a || w->hold))
		return "this widget takes no action";
	if ((t == W_SLIDER || t == W_TOGGLE || t == W_ON) && (a == A_HA || w->hold))
		return "no hold= or action=ha here";
	if ((t >= W_ENTITY || a == A_HA || w->hold == A_HA) && !w->slot)
		return "needs slot=";
	if ((a == A_HA && !ha_op_ok(arg)) || (w->hold == A_HA && !ha_op_ok(PS(c, w->harg))))
		return "bad ha operation";
	if (t == W_THERMO) {
		if (!(w->flags & WF_RANGE)) {
			w->min = 7;
			w->max = 30;
		}
		if (w->max <= w->min)
			return "needs min < max";
		if (!w->modes && (w->modes = pstr(c, "heat,off")) == 0xFFFF)
			return "string pool full";
	}
	if (a == A_SET && !w->vname)
		return "action=set needs var=";
	if (a == A_CMD && cmd_find(arg) >= CMD_RESTART)
		w->flags |= WF_CONFIRM;
	if (a == A_CMD && cmd_find(arg) < 0) {
		if (t != W_TOGGLE)
			return werr_arg = arg, "command not in allow-list: ";
		if (cmd_pair(arg, 1) < 0 || cmd_pair(arg, 0) < 0)
			return werr_arg = arg, "toggle needs NAME-on and NAME-off: ";
	}
	if (a == A_EVENT && !ident(arg))
		return "event needs arg=name ([a-z0-9_-], max 15)";
	if ((a == A_BACKLIGHT || a == A_VOLUME || a == A_BEEP || a == A_MUTE) && (w->flags & WF_ARG)) {
		int v;

		if (!pnum(arg, &v))
			return "arg must be a number";
	}
	if (t == W_BATTERY) {
		if (!w->key)
			w->key = pstr(c, "battery_pct_est");
		if (!w->sub)
			w->sub = pstr(c, "charging");
		if (!w->colorkey)
			w->colorkey = pstr(c, "external_power");
		if (w->key == 0xFFFF || w->sub == 0xFFFF || w->colorkey == 0xFFFF)
			return "string pool full";
	}
	return 0;
}

/* page dots: swipe neighbours of page i (itself included) when there are two or more and the config shows dots */
static int dot_count(const cfg_conf *c, int cur)
{
	int i, n = 0;

	if (!(c->nav & NAV_DOTS) || !peer(c, cur, cur))
		return 0;
	for (i = 0; i < c->npages; i++)
		n += peer(c, cur, i);
	return n > 1 ? n : 0;
}

/* content area: y 40 down to 248 above the dots (centred on y 260), else to 264 */
static int content_h(const cfg_conf *c, int page)
{
	return (dot_count(c, page) ? 248 : 264) - Y0;
}

/* list rows: below the caption (34 px when the list has a label), up to 42 px each */
static void layout_rows(cfg_conf *c, int li)
{
	cfg_widget *l = &c->w[li];
	int n = 0, k, top = l->label ? 34 : 0, rh;

	for (k = li + 1; k < c->nwidgets && c->w[k].type == W_ROW; k++)
		n++;
	if (!n)
		return;
	rh = (int)gui_udiv((u32)(l->h - top), (u32)n);
	if (rh > 42)
		rh = 42;
	for (k = 0; k < n; k++) {
		cfg_widget *r = &c->w[li + 1 + k];

		r->x = l->x;
		r->y = (short)(l->y + top + k * rh);
		r->w = l->w;
		r->h = (short)rh;
	}
}

static void layout(cfg_conf *c)
{
	int i;

	for (i = 0; i < c->nwidgets; i++) {
		cfg_widget *w = &c->w[i];
		int cols, rows, cellw, cellh, ch;

		if (w->type == W_ON || w->type == W_ROW || (w->flags & WF_XY))
			continue;
		ch = content_h(c, w->page);
		cols = c->page[w->page].cols;
		rows = c->page[w->page].rows;
		cellw = (int)gui_udiv((u32)(CW - GAP * (cols - 1)), (u32)cols);
		cellh = (int)gui_udiv((u32)(ch - GAP * (rows - 1)), (u32)rows);
		w->x = (short)(X0 + w->col * (cellw + GAP));
		w->y = (short)(Y0 + w->row * (cellh + GAP));
		w->w = (short)(w->cs * cellw + (w->cs - 1) * GAP);
		w->h = (short)(w->rs * cellh + (w->rs - 1) * GAP);
	}
	for (i = 0; i < c->nwidgets; i++)
		if (c->w[i].type == W_LIST)
			layout_rows(c, i);
}

static int final_check(cfg_conf *c)
{
	int i;

	if (!c->npages)
		return fail(c, 0, "no page defined", 0);
	for (i = 0; i < c->npages; i++)
		if (c->page[i].parent && find_page(c, PS(c, c->page[i].parent)) < 0)
			return fail(c, 0, "unknown parent page ", PS(c, c->page[i].parent));
	for (i = 0; i < c->nwidgets; i++) {
		cfg_widget *w = &c->w[i];
		const char *arg = PS(c, w->arg);

		if (w->slot || prefix(PS(c, w->key), "ha.") || prefix(PS(c, w->sub), "ha.") || prefix(PS(c, w->colorkey), "ha."))
			c->uses_ha = 1;
		if (w->type == W_ON || w->type == W_ROW)
			continue;
		if (w->flags & WF_XY) {
			if (!w->w || !w->h || w->x + w->w > GUI_W || w->y + w->h > GUI_H)
				return fail(c, w->line, "x/y/w/h missing or off screen", 0);
		} else if (w->col + w->cs > c->page[w->page].cols || w->row + w->rs > c->page[w->page].rows) {
			return fail(c, w->line, "at=/span= outside the page grid", 0);
		}
		if (w->action == A_PAGE && !page_known(c, arg))
			return fail(c, w->line, "unknown page ", arg);
		if (w->hold == A_PAGE && !page_known(c, PS(c, w->harg)))
			return fail(c, w->line, "unknown page ", PS(c, w->harg));
		if (w->detail && find_page(c, PS(c, w->detail)) < 0)
			return fail(c, w->line, "unknown page ", PS(c, w->detail));
	}
	return 1;
}

/* a parse error on the current line: message, detail */
#define FAIL(m, d) do { em = (m); werr_arg = (d); goto bad; } while (0)

int cfg_parse(cfg_conf *c, const char *buf, int len)
{
	static char ln[CFG_LINE_MAX + 1];
	int pos = 0, line = 0, i, nav = -1;
	const char *em;
	u16 start = 0;

	{
		char *p = (char *)c;
		u32 k;

		for (k = 0; k < sizeof(*c); k++)
			p[k] = 0;
	}
	c->pool_used = 1;	/* offset 0 is the empty string */
	c->ok = 1;
	if (len >= CFG_FILE_MAX)
		return fail(c, 0, "file larger than 12 KB", 0);
	while (pos < len) {
		char *s, *dir, *k, *v;
		int n = 0, wt;
		cfg_widget *w = 0;

		line++;
		while (pos < len && buf[pos] != '\n') {
			if (n >= CFG_LINE_MAX)
				FAIL("line longer than 200 chars", 0);
			char ch = buf[pos++];

			ln[n++] = ch == '\t' ? ' ' : ch;
		}
		pos++;
		while (n && (ln[n - 1] == '\r' || ln[n - 1] == ' '))
			n--;
		ln[n] = 0;
		s = ln;
		while (*s == ' ')
			s++;
		if (!*s || *s == '#')
			continue;
		dir = s;
		while (*s && *s != ' ')
			s++;
		if (*s)
			*s++ = 0;

		wt = lookup(wtype_name, 15, dir);
		if (wt > 0) {
			if (!c->npages && wt != W_ON)
				FAIL("widget before the first page", 0);
			if (c->nwidgets >= CFG_WIDGETS)
				FAIL("more than 64 widgets", 0);
			w = &c->w[c->nwidgets];
			wzero(w);
			w->type = (u8)wt;
			if (wt == W_THERMO)
				w->step = 5;
			w->page = (u8)(wt == W_ON ? 0xFF : c->npages - 1);
			w->line = (short)line;
			if (wt == W_LABEL)
				w->color = 1;
		} else if (seq(dir, "page")) {
			if (c->npages >= CFG_PAGES)
				FAIL("more than 20 pages", 0);
			c->page[c->npages].cols = 3;
			c->page[c->npages].rows = 2;
			c->npages++;
		} else if (!seq(dir, "ui") && !seq(dir, "var")) {
			FAIL("unknown directive ", dir);
		} else if (seq(dir, "var")) {
			if (c->nvdecl >= CFG_VARS - 3)
				FAIL("too many vars", 0);
			c->nvdecl++;
		}

		for (;;) {
			while (*s == ' ')
				s++;
			if (!*s)
				break;
			k = s;
			while (*s && *s != '=' && *s != ' ')
				s++;
			if (*s != '=') {
				/* on lines take a bare ACTION:ARG (`on key=short screen:toggle`), the same as tap= */
				if (!w || w->type != W_ON)
					FAIL("expected key=value near ", k);
				if (*s)
					*s++ = 0;
				v = k;
				k = "tap";
				goto pair;
			}
			*s++ = 0;
			if (*s == '"') {
				v = ++s;
				while (*s && *s != '"')
					s++;
				if (!*s)
					FAIL("unterminated quote", 0);
				*s++ = 0;
				if (*s && *s != ' ')
					FAIL("space expected after quote", 0);
			} else {
				v = s;
				while (*s && *s != ' ')
					s++;
				if (*s)
					*s++ = 0;
			}
pair:
			if (w) {
				werr_arg = 0;
				if ((em = wkey(c, w, k, v)))
					goto bad;
			} else if (seq(dir, "page")) {
				cfg_page *p = &c->page[c->npages - 1];

				if (seq(k, "id")) {
					if (!ident(v))
						FAIL("bad page id ", v);
					if (find_page(c, v) >= 0)
						FAIL("duplicate page id ", v);
					p->id = pstr(c, v);
				} else if (seq(k, "title")) {
					p->title = pstr(c, v);
				} else if (seq(k, "hidden")) {
					p->hidden = (u8)truthy(v);
				} else if (seq(k, "hub")) {
					p->hub = (u8)truthy(v);
				} else if (seq(k, "group") || seq(k, "parent")) {
					if (!ident(v))
						FAIL("bad value for ", k);
					if (k[0] == 'g')
						p->group = pstr(c, v);
					else
						p->parent = pstr(c, v);
				} else if (seq(k, "grid")) {
					int a, b;

					if (!ppair(v, &a, &b, 'x') || a < 1 || a > 4 || b < 1 || b > 4)
						FAIL("grid must be CxR, 1..4 each", 0);
					p->cols = (u8)a;
					p->rows = (u8)b;
				} else {
					FAIL("unknown key ", k);
				}
				if (p->id == 0xFFFF || p->title == 0xFFFF || p->group == 0xFFFF || p->parent == 0xFFFF)
					FAIL("string pool full", 0);
			} else if (seq(dir, "var")) {
				int x = c->nvdecl - 1;

				if (seq(k, "name")) {
					if (!ident(v))
						FAIL("bad var name ", v);
					c->vdecl[x] = pstr(c, v);
				} else if (seq(k, "value")) {
					if (!pnum(v, &c->vinit[x]))
						FAIL("bad number ", v);
				} else {
					FAIL("unknown key ", k);
				}
			} else {
				if (seq(k, "title")) {
					c->title = pstr(c, v);
				} else if (seq(k, "nav")) {
					/* tabs, dots, both, none; 0 and 1 (older files) mean none and both */
					if ((nav = lookup(nav_name, 6, v)) < 0)
						FAIL("nav must be tabs/dots/both/none", 0);
					nav = nav_val[nav];
				} else if (seq(k, "wrap")) {
					c->wrap = truthy(v);
				} else if (seq(k, "theme")) {
					if (!seq(v, "dark") && !seq(v, "light"))
						FAIL("theme must be dark or light", 0);
					c->theme = v[0] == 'l';
				} else if (seq(k, "tz")) {
					if (!tz_parse(v, &c->tz, &c->tzeu) || c->tzeu < 0)
						FAIL("bad value for ", k);
				} else if (seq(k, "autolock")) {
					if (!pnum(v, &c->autolock) || (c->autolock && (c->autolock < 10 || c->autolock > 3600)))
						FAIL("autolock must be 0 or 10..3600", 0);
				} else if (seq(k, "anim")) {
					if (!seq(v, "0"))
						FAIL("anim=1 is not implemented", 0);
				} else if (seq(k, "start")) {
					start = pstr(c, v);
				} else if (seq(k, "version")) {
					if (!seq(v, "1"))
						FAIL("unsupported version ", v);
				} else {
					FAIL("unknown key ", k);
				}
				if (c->title == 0xFFFF || start == 0xFFFF)
					FAIL("string pool full", 0);
			}
		}
		if (w) {
			werr_arg = 0;
			if ((em = wcheck_msg(c, w)))
				goto bad;
			c->nwidgets++;
		} else if (seq(dir, "page")) {
			cfg_page *p = &c->page[c->npages - 1];

			if (!p->id)
				FAIL("page needs id=", 0);
			if (!p->title)
				p->title = p->id;
			if (p->hub && p->hidden)
				FAIL("a hub page cannot be hidden", 0);
			if (!p->hidden && !p->hub && ++c->nvis > CFG_NAV)
				FAIL("more than 6 visible pages", 0);
		} else if (seq(dir, "var") && !c->vdecl[c->nvdecl - 1]) {
			FAIL("var needs name=", 0);
		}
	}
	c->nav = nav < 0 ? (c->nvis > 1 ? NAV_TABS : 0) | NAV_DOTS : nav;
	c->hub = -1;
	for (i = c->npages - 1; i >= 0; i--)
		if (c->page[i].hub)
			c->hub = i;
	c->start = 0;
	while (c->start < c->npages - 1 && c->page[c->start].hidden)
		c->start++;
	if (start) {
		i = find_page(c, PS(c, start));
		if (i < 0)
			return fail(c, 0, "unknown start page ", PS(c, start));
		c->start = i;
	}
	if (c->page[c->start].hidden)
		return fail(c, 0, "start page is hidden", 0);
	if (!final_check(c))
		return 0;
	layout(c);
	for (i = 0; i < c->nwidgets; i++) {
		cfg_widget *w = &c->w[i];

		/* a full thermostat card (150 px high or more) may be 140 wide, as the spec's 156 px side column */
		if (w->type >= W_LIGHTCTL && (w->w < 200 || w->h < 40) && !(w->type == W_THERMO && w->w >= 140 && w->h >= 150))
			return fail(c, w->line, "too small (min 200x40 px)", 0);
	}
	return 1;
bad:
	return fail(c, line, em, werr_arg);
}

/* ---------- runtime state ---------- */

static int var_find(const cfg_ctx *x, const char *name)
{
	int i;

	for (i = 0; i < x->nvars; i++)
		if (seq(x->var[i].name, name))
			return i;
	return -1;
}

static int var_add(cfg_ctx *x, const char *name, int value)
{
	int i = var_find(x, name);

	if (i >= 0)
		return i;
	if (x->nvars >= CFG_VARS)
		return -1;
	i = x->nvars++;
	scopy(x->var[i].name, name, sizeof(x->var[i].name));
	x->var[i].value = value;
	return i;
}

static int var_get(const cfg_ctx *x, const char *name)
{
	int i = var_find(x, name);

	return i < 0 ? 0 : x->var[i].value;
}

/* built-in config: conf/default.conf without its comments (tests/run.sh checks that the two agree) */
static const char builtin_conf[] =
	"ui version=1 title=homtom\n"
	"on key=short screen:toggle\n"
	"on key=long page:system\n"
	"page id=system title=System hub=1 grid=3x2\n"
	"button at=0,0 label=Display icon=brightness-6 action=page arg=display key=var.backlight unit=\" % brightness\"\n"
	"button at=1,0 label=GPS icon=crosshairs-gps action=page arg=gps key=gps.fix\n"
	"button at=2,0 label=Device icon=information-outline action=page arg=device key=battery_pct_est unit=\" % battery\"\n"
	"page id=display title=Display grid=1x2 hidden=1 parent=system\n"
	"slider at=0,0 label=Brightness action=backlight min=5 max=100 step=5 unit=\" %\"\n"
	"page id=gps title=GPS hidden=1 parent=system\n"
	"list x=8 y=40 w=464 h=168\n"
	"row label=Fix key=gps.fix\n"
	"row label=Satellites key=gps.sats\n"
	"row label=\"UTC time\" key=gps.utc\n"
	"row label=Altitude key=gps.alt unit=\" m\"\n"
	"page id=device title=Device hidden=1 parent=system\n"
	"list x=8 y=40 w=464 h=210\n"
	"row label=Battery key=battery_pct_est unit=\" %\" low=16\n"
	"row label=Mains key=external_power fmt=yesno\n"
	"row label=Charging key=charging fmt=yesno\n"
	"row label=Uptime key=uptime\n"
	"row label=\"Ambient light\" key=light_raw unavailable_below=10\n"
;

void cfg_init(cfg_ctx *x)
{
	char *p = (char *)x;
	u32 i;

	for (i = 0; i < sizeof(*x); i++)
		p[i] = 0;
	ui_init(&x->st);
	x->press = x->drag = x->confirm = x->hcand = x->hage = -1;
	x->hpool_used = 1;
	var_add(x, "backlight", 100);
	var_add(x, "volume", 60);
	var_add(x, "mute", 0);
	cfg_use(x, 0);
}

void cfg_use(cfg_ctx *x, cfg_conf *c)
{
	static cfg_conf builtin;
	int i, keep = x->c != 0, old = x->page;

	if (!c) {
		if (!builtin.ok)
			cfg_parse(&builtin, builtin_conf, (int)sizeof(builtin_conf) - 1);
		c = &builtin;
	}
	x->builtin = c == &builtin;

	x->press = x->drag = x->confirm = x->hcand = -1;
	x->swallow = 0;
	x->st.press = -1;
	x->st.drag = 0;
	x->c = c;
	for (i = 0; i < c->nvdecl; i++)
		var_add(x, PS(c, c->vdecl[i]), c->vinit[i]);
	for (i = 0; i < c->nwidgets; i++)
		c->w[i].var = (short)(c->w[i].vname ? var_add(x, PS(c, c->w[i].vname), 0) : -1);
	x->page = x->prev = keep && old < c->npages ? old : c->start;
	x->area = c->page[x->page].hidden || c->page[x->page].hub ? 0 : x->page;
	while (x->area < c->npages - 1 && (c->page[x->area].hidden || c->page[x->area].hub))
		x->area++;
}

void cfg_go(cfg_ctx *x, int p)
{
	if (p != x->page) {
		x->prev = x->page;
		x->page = p;
		x->lcmore = 0;
	}
	if (!x->c->page[p].hidden && !x->c->page[p].hub)
		x->area = p;
}

/* INTERNAL:/homtom/tz: one line, auto / MIN / MIN,eu / MIN,us; anything else counts as missing */
void cfg_tzfile(cfg_ctx *x, const char *buf, int len)
{
	char b[16];
	int i, a, r;

	for (i = 0; i < len && i < 15 && buf[i] != '\n' && buf[i] != '\r'; i++)
		b[i] = buf[i];
	b[i] = 0;
	x->tzset[0] = 0;
	if (len >= 0 && tz_parse(b, &a, &r))
		scopy(x->tzset, b, sizeof(x->tzset));
}

int cfg_page_count(const cfg_ctx *x)
{
	return x->c->npages;
}

const char *cfg_page_id(const cfg_ctx *x, int i)
{
	return PS(x->c, x->c->page[i].id);
}

void cfg_set_note(cfg_ctx *x, const char *a, const char *b)
{
	char *p = x->note;
	int n = 0;

	while (a && *a && n < CFG_ERR - 1)
		p[n++] = *a++;
	while (b && *b && n < CFG_ERR - 1)
		p[n++] = *b++;
	p[n] = 0;
	x->note_ttl = n ? 8 : 0;
}

void cfg_status(cfg_ctx *x, const char *buf, int len)
{
	int i = 0;

	ui_parse(&x->st, buf, len);
	x->nkeys = 0;
	x->kpool_used = 1;
	x->kpool[0] = 0;
	while (i < len && x->nkeys < CFG_KEYS) {
		int ks = i, ke, vs, ve, n;

		while (i < len && buf[i] != '=' && buf[i] != '\n')
			i++;
		ke = i;
		if (i >= len || buf[i] == '\n') {
			i++;
			continue;
		}
		vs = ++i;
		while (i < len && buf[i] != '\n' && buf[i] != '\r')
			i++;
		ve = i;
		if (ve - vs > 96)
			ve = vs + 96;
		if (ke - ks > 31 || ke == ks || x->kpool_used + (ke - ks) + (ve - vs) + 2 > CFG_KPOOL)
			continue;
		x->kname[x->nkeys] = (u16)x->kpool_used;
		for (n = ks; n < ke; n++)
			x->kpool[x->kpool_used++] = buf[n];
		x->kpool[x->kpool_used++] = 0;
		x->kval[x->nkeys] = (u16)x->kpool_used;
		for (n = vs; n < ve; n++)
			x->kpool[x->kpool_used++] = buf[n];
		x->kpool[x->kpool_used++] = 0;
		x->nkeys++;
	}
}

/* ---------- Home Assistant states (docs/HA_UI.md) ---------- */

static const char *ha_file(const cfg_ctx *x, const char *k)
{
	int i;

	for (i = 0; i < x->hn; i++)
		if (seq(x->hpool + x->hname[i], k))
			return x->hpool + x->hval[i];
	return 0;
}

/* SLOT.FIELD: the optimistic value while one is pending, else the file's */
static const char *ha_get(const cfg_ctx *x, const char *k)
{
	int i;

	for (i = 0; i < CFG_HA_OPT; i++)
		if (x->opt[i].k[0] && seq(x->opt[i].k, k))
			return x->opt[i].v;
	return ha_file(x, k);
}

/* "SLOT.FIELD" into b (>= 40 bytes); slot "*" is the slot that opened the page. 0 when there is no slot */
static char *ha_key(const cfg_ctx *x, const char *slot, const char *field, char *b)
{
	char *p;

	if (slot[0] == '*')
		slot = x->dslot;
	if (!slot[0])
		return 0;
	p = gui_cat(b, slot);
	*p++ = '.';
	scopy(p, field, 16);
	return b;
}

static const char *hf(const cfg_ctx *x, const char *slot, const char *field)
{
	char b[40];

	return ha_key(x, slot, field, b) ? ha_get(x, b) : 0;
}

static int hnum(const cfg_ctx *x, const char *slot, const char *field, int *n)
{
	const char *v = hf(x, slot, field);

	return v && whole(v, n);
}

static const char *const on_words[] = { "on", "home", "open", "heat", "cool", "heat_cool", "auto", "dry", "fan_only" };

/* 1 on (or an active mode), 0 off or another state, -1 unknown/unavailable */
static int on_state(const char *v)
{
	if (!v || !*v || seq(v, "unavailable") || seq(v, "unknown"))
		return -1;
	return lookup(on_words, 9, v) >= 0;
}

/* /tmp/ha.states: SLOT.FIELD=value lines, an optional leading "ha." is dropped. Lines longer than 80 characters, keys
   outside [a-z0-9_.] and values with control characters are skipped; a line cut by the 4 KB read limit is ignored */
void cfg_ha(cfg_ctx *x, const char *buf, int len, int age)
{
	int i = 0, j;

	x->hn = 0;
	x->hpool_used = 1;
	x->hage = len < 0 ? -1 : age;
	if (len > CFG_HA_MAX)
		len = CFG_HA_MAX;
	while (i < len && x->hn < CFG_HA_KEYS) {
		int s = i, e = i, eq = -1, ok = 1, k;

		for (; e < len && buf[e] != '\n'; e++) {
			char ch = buf[e];

			if (eq < 0 && ch == '=')
				eq = e;
			else if (eq < 0 ? !((ch >= 'a' && ch <= 'z') || is_digit(ch) || ch == '_' || ch == '.') : ch < 32 || ch > 126)
				ok = 0;
		}
		i = e + 1;
		if (e == len && len == CFG_HA_MAX)
			break;
		if (e - s > CFG_HA_LINE)
			continue;
		if (eq - s > 3 && buf[s] == 'h' && buf[s + 1] == 'a' && buf[s + 2] == '.')
			s += 3;
		if (!ok || eq <= s || eq - s > 31)
			continue;
		k = x->hpool_used;
		x->hname[x->hn] = (u16)k;
		x->hval[x->hn++] = (u16)(k + eq - s + 1);
		for (j = s; j < e; j++)
			x->hpool[k++] = j == eq ? 0 : buf[j];
		x->hpool[k++] = 0;
		x->hpool_used = k;
	}
	for (j = 0; j < CFG_HA_OPT; j++) {
		const char *v = x->opt[j].k[0] ? ha_file(x, x->opt[j].k) : 0;

		if (v && seq(v, x->opt[j].v))
			x->opt[j].k[0] = 0;	/* confirmed */
	}
}

/* ---------- values ---------- */

static void uptime_fmt(char *d, u32 up)
{
	u32 days = up / 86400, hrs = up / 3600 % 24, mins = up / 60 % 60;

	if (days) {
		d = gui_utoa(d, days);
		d = gui_cat(d, "d ");
		d = gui_utoa(d, hrs);
		gui_cat(d, "h");
	} else if (hrs) {
		d = gui_utoa(d, hrs);
		d = gui_cat(d, "h ");
		d = gui_utoa(d, mins);
		gui_cat(d, "m");
	} else {
		d = gui_utoa(d, mins);
		gui_cat(d, "m");
	}
}

/* a key exactly as the status file has it; 0 when absent */
static const char *rawval(const cfg_ctx *x, const char *k)
{
	int i;

	for (i = 0; i < x->nkeys; i++)
		if (seq(x->kpool + x->kname[i], k))
			return x->kpool + x->kval[i];
	return 0;
}

/* bt.visible: "4:35 left" while the pairing window is open, else the state homtom-bt-visible wrote (closed, paired, ...) */
static const char *bt_visible(const cfg_ctx *x, char *b)
{
	const char *s = rawval(x, "bt_visible_s"), *t = rawval(x, "bt_visible");
	int n;
	char *p;

	if (s && whole(s, &n) && n > 0) {
		p = gui_utoa(b, (u32)n / 60);
		*p++ = ':';
		p = gui_pad2(p, (u32)n % 60);
		gui_cat(p, " left");
		return b;
	}
	if (t && *t)
		return t;
	return s ? "closed" : 0;
}

/* mic.result: "-38 dB" while the meter runs (live level), then "OK" or "quiet"; "starting" before the first line; "error"
   (homtom-mic-test, 20 s meter) */
static const char *mic_result(const cfg_ctx *x, char *b)
{
	const char *l = rawval(x, "mic_level"), *s = rawval(x, "mic_state");

	if (!s)
		return 0;
	if (seq(s, "ok"))
		return "OK";
	if (seq(s, "quiet") || seq(s, "error"))
		return s;
	if (!seq(s, "running") || !l || !*l)
		return seq(s, "running") ? "starting" : s;
	scopy(b, l, 8);
	gui_cat(b + slen(b), " dB");
	return b;
}

/* mic.sub: "pk -12 dB  18 s" while it runs; "pk -12 dB  last -40" when done; 0 when there is nothing to say */
static const char *mic_sub(const cfg_ctx *x, char *b)
{
	const char *l = rawval(x, "mic_level"), *p = rawval(x, "mic_peak"), *t = rawval(x, "mic_t"), *s = rawval(x, "mic_state");
	char *d = b;

	if (!s || !p || !*p || seq(s, "error"))
		return 0;
	d = gui_cat(d, "pk ");
	scopy(d, p, 8);
	d = gui_cat(d + slen(d), " dB");
	if (seq(s, "running") && t && *t) {
		d = gui_cat(d, "  ");
		scopy(d, t, 4);
		gui_cat(d + slen(d), " s");
	} else if (l && *l && !seq(s, "running")) {
		d = gui_cat(d, "  last ");
		scopy(d, l, 8);
	}
	return b;
}

static const char *time_key(const cfg_ctx *x, const char *k, char *b);

/* raw string value of a status key, a derived gps./light./bt.visible/mic. key or var.NAME; 0 when unknown. b holds >= 40 chars */
static const char *value(const cfg_ctx *x, const char *k, char *b)
{
	const ui_state *s = &x->st;
	int i;

	if (!*k)
		return 0;
	if (prefix(k, "var.")) {
		i = var_find(x, k + 4);
		if (i < 0)
			return 0;
		itoa_s(b, x->var[i].value);
		return b;
	}
	if (seq(k, "page"))
		return PS(x->c, x->c->page[x->page].title);
	if (seq(k, "clock") || seq(k, "date") || prefix(k, "tz."))
		return time_key(x, k, b);
	if (prefix(k, "ha.")) {
		char slot[16];

		for (k += 3, i = 0; k[i] && k[i] != '.' && i < 15; i++)
			slot[i] = k[i];
		slot[i] = 0;
		if (k[i] != '.')
			return 0;
		k += i + 1;
		if (!seq(k, "on"))
			return hf(x, slot, k);
		i = on_state(hf(x, slot, "state"));
		return i < 0 ? 0 : i ? "1" : "0";
	}
	if (!s->have)
		return 0;
	if (seq(k, "uptime")) {
		uptime_fmt(b, s->up);
		return b;
	}
	if (seq(k, "light.pct")) {
		if (s->light < 0)
			return 0;
		gui_utoa(b, (u32)(s->light > 1023 ? 1023 : s->light) * 100 / 1023);
		return b;
	}
	if (prefix(k, "gps.") && (i = lookup(gps_key, 8, k + 4)) >= 0) {
		/* 0 fix 1 fixq 2 sats 3 utc; 4 alt 5 speed 6 course 7 hdop in tenths, -1 when unknown */
		int t[4] = { s->fixq <= 0 ? -1 : s->alt10, s->rmc_ok ? s->spd10 : 0, s->rmc_ok ? s->crs10 : -1, s->hdop10 };

		if (i == 0)
			return s->fixq <= 0 ? "No fix" : s->fixq == 2 ? "DGPS fix" : "GPS fix";
		if (i < 3) {
			itoa_s(b, i == 1 ? s->fixq : s->sats);
		} else if (i == 3) {
			char *p;

			if (s->hh < 0)
				return 0;
			p = gui_pad2(b, (u32)s->hh);
			*p++ = ':';
			p = gui_pad2(p, (u32)s->mm);
			*p++ = ':';
			gui_pad2(p, (u32)s->ss);
		} else {
			if (t[i - 4] < 0)
				return 0;
			gui_ftoa(b, t[i - 4]);
		}
		return b;
	}
	if (seq(k, "bt.visible"))
		return bt_visible(x, b);
	if (seq(k, "mic.result"))
		return mic_result(x, b);
	if (seq(k, "mic.sub"))
		return mic_sub(x, b);
	if (seq(k, "mic.ok")) {
		const char *m = rawval(x, "mic_state");

		return !m ? 0 : seq(m, "ok") ? "1" : "0";
	}
	return rawval(x, k);
}

static int numval(const cfg_ctx *x, const char *k, int *out)
{
	char b[48];
	const char *v = value(x, k, b);

	if (!v)
		return 0;
	if (v[0] == 't' || v[0] == 'T' || v[0] == 'y' || (v[0] == 'o' && v[1] == 'n')) {
		*out = 1;
		return 1;
	}
	if (v[0] == 'f' || v[0] == 'n' || (v[0] == 'o' && v[1] == 'f')) {
		*out = 0;
		return 1;
	}
	return whole(v, out);
}

static int pct_of(int v, int min, int max)
{
	v = clampi(v, min, max);
	return max > min ? (int)gui_udiv((u32)(v - min) * 100, (u32)(max - min)) : 0;
}

/* formatted value plus unit into d (>= 48 bytes); returns 1 when known, else d = "" */
static int fmt_value(const cfg_ctx *x, const cfg_widget *w, u16 key, u16 unit, char *d)
{
	const cfg_conf *c = x->c;
	char b[48];
	const char *v = value(x, PS(c, key), b);
	int n;
	char *p = d;

	if (!v) {
		d[0] = 0;
		return 0;
	}
	switch (key == w->key ? w->fmt : F_TEXT) {
	case F_INT:
		if (!whole(v, &n))
			goto text;
		p = itoa_s(d, n);
		break;
	case F_TENTHS:
		if (!tenths(v, &n))
			goto text;
		p = gui_ftoa(d, n);
		break;
	case F_YESNO:
	case F_ONOFF:
		n = truthy(v);
		p = gui_cat(d, w->fmt == F_YESNO ? (n ? "Yes" : "No") : (n ? "On" : "Off"));
		break;
	case F_PCT:
		if (!whole(v, &n))
			goto text;
		p = gui_utoa(d, (u32)pct_of(n, w->min, w->max));
		break;
	case F_UPTIME:
		if (!whole(v, &n) || n < 0)
			goto text;
		uptime_fmt(d, (u32)n);
		p = d + slen(d);
		break;
	default:
	text:
		scopy(d, v, 40);
		p = d + slen(d);
		break;
	}
	if (unit)
		scopy(p, PS(c, unit), 47 - (int)(p - d));
	return 1;
}

/* high= / low= / good= / good_below= thresholds: the colour role of a numeric value (good is the normal colour) */
static int thr(const cfg_widget *w, int n, int def)
{
	if ((w->flags & WF_HIGH) && n >= w->high)
		return T_ERROR;
	if ((w->flags & WF_LOW) && n < w->low)
		return T_ERROR;
	if (((w->flags & WF_GOOD) && n >= w->good) || ((w->flags & WF_GBELOW) && n < w->gbelow))
		return T_TEXT;
	return def;
}

static int value_role(const cfg_ctx *x, const cfg_widget *w, int known)
{
	int n;

	if (!known)
		return T_TEXT2;
	if (!numval(x, PS(x->c, w->colorkey ? w->colorkey : w->key), &n))
		return col_role[w->color];
	return thr(w, n, col_role[w->color]);
}

/* unavailable_below=N: the numeric value of key= is below N, so the reading means nothing (a sensor not fitted) */
static int not_avail(const cfg_ctx *x, const cfg_widget *w)
{
	int n;

	return (w->flags & WF_NA) && numval(x, PS(x->c, w->key), &n) && n < w->nabelow;
}

/* ---------- time: header clock, Time & date page ---------- */

#define Y2026 1767225600u	/* 2026-01-01 00:00 UTC: an earlier device clock is not set */

/* days since 1970-01-01 to year, month (1..12), day; weekday 0 = Sunday */
static void civil(u32 days, int *y, int *m, int *d)
{
	u32 z = days + 719468, era = z / 146097, doe = z - era * 146097;
	u32 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	u32 mp = (5 * doy + 2) / 153;

	*d = (int)(doy - (153 * mp + 2) / 5 + 1);
	*m = (int)(mp < 10 ? mp + 3 : mp - 9);
	*y = (int)(yoe + era * 400 + (*m <= 2));
}

static u32 days_of(int y, int m, int d)
{
	u32 yy = (u32)(m <= 2 ? y - 1 : y), era = yy / 400, yoe = yy - era * 400;
	u32 doy = (153 * (u32)(m > 2 ? m - 3 : m + 9) + 2) / 5 + (u32)d - 1;

	return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}

/* day (days since 1970) of the n-th Sunday of y-m, n = 0 for the last one */
static u32 sunday(int y, int m, int n)
{
	u32 d;

	if (!n) {
		d = days_of(y, m + 1, 1) - 1;
		return d - (d + 4) % 7;
	}
	d = days_of(y, m, 1);
	return d + (7 - (d + 4) % 7) % 7 + 7 * (u32)(n - 1);
}

/* offset in minutes at UTC time t for a base offset and rule: 0 none, 1 EU (last Sunday of March 01:00 UTC to last Sunday
   of October 01:00 UTC), 2 US (second Sunday of March 02:00 local standard time to first Sunday of November 02:00 local
   daylight time) */
int cfg_tz_offset(int base, int rule, u32 t)
{
	int y, m, d;
	u32 a, b;

	if (!rule)
		return base;
	civil(t / 86400, &y, &m, &d);
	if (rule == 1) {
		a = sunday(y, 3, 0) * 86400 + 3600;
		b = sunday(y, 10, 0) * 86400 + 3600;
	} else {
		a = sunday(y, 3, 2) * 86400 + 7200 - (u32)(base * 60);
		b = sunday(y, 11, 1) * 86400 + 7200 - (u32)((base + 60) * 60);
	}
	return t >= a && t < b ? base + 60 : base;
}

/* the offset in use and where it came from: 1 the device file, 2 Home Assistant, 3 ui tz=, 0 none (UTC) */
static int tz_now(const cfg_ctx *x, int *off)
{
	int min, rule, n;
	const char *h = hf(x, "sys", "tz_off");

	if (x->tzset[0] && tz_parse(x->tzset, &min, &rule) && rule >= 0) {
		*off = cfg_tz_offset(min, rule, x->wall);
		return 1;
	}
	if (h && pnum(h, &n) && n >= -720 && n <= 840) {
		*off = n;
		return 2;
	}
	*off = cfg_tz_offset(x->c->tz, x->c->tzeu, x->wall);
	return x->c->tz || x->c->tzeu ? 3 : 0;
}

/* "UTC+01:00" */
static char *utc_str(char *b, int off)
{
	char *p = gui_cat(b, "UTC");

	*p++ = off < 0 ? '-' : '+';
	off = off < 0 ? -off : off;
	p = gui_pad2(p, (u32)off / 60);
	*p++ = ':';
	return gui_pad2(p, (u32)off % 60);
}

static const char *const wday[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static const char *const mon[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September",
				   "October", "November", "December" };

/* keys clock, date, tz.source, tz.zone, tz.manual (value()); 0 when the clock is not set */
static const char *time_key(const cfg_ctx *x, const char *k, char *b)
{
	int off, src = tz_now(x, &off), y, m, d;
	u32 t = x->wall + (u32)(off * 60);
	char *p;

	if (seq(k, "tz.source"))
		return src == 1 ? "Set on device" : src == 2 ? "Auto - Home Assistant" : src == 3 ? "Config default" : "UTC";
	if (seq(k, "tz.manual")) {
		utc_str(gui_cat(b, "Fixed offset "), x->tzset[0] && tz_parse(x->tzset, &y, &m) && m == 0 ? y : 0);
		return b;
	}
	if (seq(k, "tz.zone")) {
		const char *n = src == 2 ? hf(x, "sys", "tz_name") : 0;

		p = b;
		if (n && *n) {
			scopy(b, n, 24);
			p = gui_cat(b + slen(b), ", ");
		}
		utc_str(p, off);
		return b;
	}
	if (x->wall < Y2026)
		return 0;
	if (seq(k, "clock")) {
		p = gui_pad2(b, t / 3600 % 24);
		*p++ = ':';
		gui_pad2(p, t / 60 % 60);
		return b;
	}
	civil(t / 86400, &y, &m, &d);
	p = gui_cat(b, wday[(t / 86400 + 4) % 7]);
	*p++ = ' ';
	p = gui_utoa(p, (u32)d);
	*p++ = ' ';
	p = gui_cat(p, mon[m - 1]);
	*p++ = ' ';
	gui_utoa(p, (u32)y);
	return b;
}

/* ---------- drawing ---------- */

static const short sin5[19] = { 0, 357, 711, 1060, 1401, 1731, 2048, 2349, 2633, 2896, 3138, 3355, 3547, 3712, 3849, 3956,
				4034, 4080, 4096 };

/* sin(deg) in Q12, deg a multiple of 5 */
static int isin(int deg)
{
	while (deg < 0)
		deg += 360;
	while (deg >= 360)
		deg -= 360;
	if (deg <= 90)
		return sin5[deg / 5];
	if (deg <= 180)
		return sin5[(180 - deg) / 5];
	if (deg <= 270)
		return -sin5[(deg - 180) / 5];
	return -sin5[(360 - deg) / 5];
}

/* arc from a0 to a1 degrees (0 = +x, clockwise on screen), centre and radius in px, width in 1/8 px */
static void arc(int cx, int cy, int r, int a0, int a1, int w8, u16 c)
{
	int a, px = 0, py = 0, first = 1;

	for (a = a0; a <= a1; a += 5) {
		int qx = cx * 8 + ((r * 8 * isin(a + 90)) >> 12), qy = cy * 8 + ((r * 8 * isin(a)) >> 12);

		if (!first)
			gui_stroke_f(px, py, qx, qy, w8, c);
		px = qx;
		py = qy;
		first = 0;
	}
}

static int is_active(const cfg_ctx *x, const cfg_widget *w)
{
	const char *arg = PS(x->c, w->arg);

	if (w->action == A_PAGE)
		return find_page(x->c, arg) == x->page;
	if (w->action == A_TZ)
		return prefix(arg, "set:") && seq(arg + 4, x->tzset[0] ? x->tzset : "auto");
	if (w->var >= 0 && (w->type == W_TOGGLE || w->action == A_MUTE))
		return x->var[w->var].value != 0;
	return 0;
}

/* hue 0..359, saturation 0..100 at full value */
static u16 hsv(int h, int s)
{
	int ch[3], i;

	for (i = 0; i < 3; i++) {
		int k = (5 - 2 * i) * 60 + h;

		if (k >= 360)
			k -= 360;
		ch[i] = 255 - clampi(imin(k, 240 - k), 0, 60) * 255 * s / 6000;
	}
	return RGB(ch[0], ch[1], ch[2]);
}

static int ha_stale(const cfg_ctx *x)
{
	return x->hage < 0 || x->hage > CFG_HA_STALE;
}

/* the HA link is down: state colours drop to grey (docs/UI_DESIGN.md, States) */
static int offline(const cfg_ctx *x)
{
	return x->c->uses_ha && ha_stale(x);
}

/* colour of a state role, greyed while offline */
static u16 sc(const cfg_ctx *x, int role)
{
	if (offline(x)) {
		if (role >= T_AMBER && role <= T_GREEN)
			role = T_TEXT2;
		else if (role >= T_CHIP_AMBER && role <= T_CHIP_PRIMARY)
			role = T_CHIP_OFF;
		else if (role == T_LIGHT_FILL)
			role = T_CARD;
		else if (role == T_PRIMARY)
			role = T_DISABLED;
	}
	return TK(role);
}

/* colour of a light: hue/sat with the saturation raised to at least 40 % (as HA's tile), else amber */
static u16 light_col(const cfg_ctx *x, const char *slot)
{
	int h, s;

	if (offline(x))
		return TK(T_TEXT2);
	if (hnum(x, slot, "hue", &h) && hnum(x, slot, "sat", &s))
		return hsv(clampi(h, 0, 359), clampi(s, 40, 100));
	return TK(T_AMBER);
}

static const char *ent_name(const cfg_ctx *x, const cfg_widget *w)
{
	const char *s = PS(x->c, w->slot), *n = hf(x, s, "name");

	return w->label ? PS(x->c, w->label) : n ? n : s[0] == '*' ? x->dslot : s;
}

/* an optimistic value of this slot is shown (sent, not yet confirmed) or a send is waiting */
static int pending(const cfg_ctx *x, const char *slot)
{
	int i, n;

	if (slot[0] == '*')
		slot = x->dslot;
	n = slen(slot);
	if (x->pend && seq(x->pslot, slot))
		return 1;
	for (i = 0; i < CFG_HA_OPT; i++)
		if (x->opt[i].k[0] && prefix(x->opt[i].k, slot) && x->opt[i].k[n] == '.')
			return 1;
	return 0;
}

/* units as the states file writes them (ASCII) to how they are shown */
static const char *unit_disp(const char *u)
{
	if (!u)
		return "";
	if (seq(u, "C"))
		return "°C";
	if (seq(u, "F"))
		return "°F";
	if (seq(u, "ug/m3"))
		return "µg/m³";
	return u;
}

/* "Changed 4 min ago": prefix, then the age in seconds as HA says it */
static char *ago(char *d, const char *pre, int s)
{
	d = gui_cat(d, pre);
	if (s < 60)
		return gui_cat(d, *pre ? "just now" : "Just now");
	d = gui_utoa(d, (u32)(s < 3600 ? s / 60 : s < 86400 ? s / 3600 : s / 86400));
	return gui_cat(d, s < 3600 ? " min ago" : s < 86400 ? " h ago" : " d ago");
}

/* entity kinds, from the states fields and the widget's action (the states file carries no HA domain) */
enum { K_SWITCH, K_LIGHT, K_CLIMATE, K_PRESS, K_BINARY, K_SENSOR, K_PERSON };

static int ent_kind(const cfg_ctx *x, const cfg_widget *w, const char *s)
{
	const char *st = hf(x, s, "state");
	int n;

	if (w->icon == ICON_LIGHTBULB + 1 || hf(x, s, "bri") || hf(x, s, "ct") || hf(x, s, "hue"))
		return K_LIGHT;
	if (hf(x, s, "target") || hf(x, s, "mode"))
		return K_CLIMATE;
	if (w->action == A_HA && seq(PS(x->c, w->arg), "press"))
		return K_PRESS;
	if (st && (seq(st, "home") || seq(st, "not_home")))
		return K_PERSON;
	if (hf(x, s, "val") || (st && tenths(st, &n)) || (!st && !w->action && !w->hold))
		return K_SENSOR;
	if (!w->action && !w->hold && st && (seq(st, "on") || seq(st, "off")))
		return K_BINARY;
	return K_SWITCH;
}

static int ent_icon(const cfg_ctx *x, const cfg_widget *w, const char *s, int kind, int on)
{
	const char *v = hf(x, s, "icon"), *u = hf(x, s, "unit");
	int id;

	if (w->icon) {
		id = w->icon - 1;
		if (id == ICON_LIGHTBULB && on <= 0)
			return ICON_LIGHTBULB_OUTLINE;
		if (id == ICON_MOTION_SENSOR && on == 0)
			return ICON_MOTION_SENSOR_OFF;
		return id;
	}
	if (v && (id = gui_icon_find(prefix(v, "mdi:") ? v + 4 : v)) >= 0)
		return id;
	switch (kind) {
	case K_LIGHT:
		return on > 0 ? ICON_LIGHTBULB : ICON_LIGHTBULB_OUTLINE;
	case K_CLIMATE:
		return ICON_THERMOSTAT;
	case K_PRESS:
		return ICON_GESTURE_TAP_BUTTON;
	case K_BINARY:
		return on > 0 ? ICON_MOTION_SENSOR : ICON_MOTION_SENSOR_OFF;
	case K_PERSON:
		return ICON_ACCOUNT;
	case K_SENSOR:
		if (u && (seq(u, "C") || seq(u, "F")))
			return ICON_THERMOMETER;
		if (u && seq(u, "%"))
			return ICON_WATER_PERCENT;
		if (u && seq(u, "ppm"))
			return ICON_MOLECULE_CO2;
		if (u && seq(u, "ug/m3"))
			return ICON_MOLECULE;
		return ICON_EYE;
	}
	return ICON_POWER;
}

/* state colour role of an entity (its chip role is role + T_CHIP_AMBER - T_AMBER, or chip_off) */
static int ent_role(const cfg_ctx *x, const char *s, int kind, int on)
{
	const char *m = hf(x, s, "mode");

	if (kind == K_SENSOR)
		return T_STEEL;
	if (kind == K_PRESS)
		return T_PRIMARY;
	if (on <= 0)
		return T_TEXT2;
	if (kind == K_CLIMATE)
		return m && seq(m, "cool") ? T_COOL : m && seq(m, "off") ? T_TEXT2 : T_HEAT;
	return kind == K_PERSON ? T_GREEN : T_AMBER;
}

static int chip_role(int role)
{
	return role == T_PRIMARY ? T_CHIP_PRIMARY : role >= T_AMBER && role <= T_GREEN ? role + T_CHIP_AMBER - T_AMBER :
	       T_CHIP_OFF;
}

/* "Heating" for climate states */
static const char *hvac(const char *m)
{
	return !m ? "Unknown" : seq(m, "heat") ? "Heating" : seq(m, "cool") ? "Cooling" : seq(m, "off") ? "Off" :
	       seq(m, "auto") ? "Auto" : m;
}

/* first letter upper case ("home" -> "Home") into b */
static char *cap(char *b, const char *s)
{
	scopy(b, s, 24);
	if (b[0] >= 'a' && b[0] <= 'z')
		b[0] = (char)(b[0] - 32);
	return b + slen(b);
}

/* state line of an entity into b (>= 48); returns 0 normal, 1 no reading, 2 unavailable */
static int ent_state(const cfg_ctx *x, const cfg_widget *w, const char *s, int kind, int on, char *b)
{
	const char *st = hf(x, s, "state"), *val = hf(x, s, "val"), *a = hf(x, s, "age");
	char *p = b;
	int n;

	if (!val && st && tenths(st, &n) && kind == K_SENSOR)
		val = st;
	if (st && (seq(st, "unavailable") || seq(st, "unknown"))) {
		gui_cat(b, "Unavailable");
		return 2;
	}
	if (kind == K_SENSOR && val) {
		p = gui_cat(b, val);
		if (*unit_disp(hf(x, s, "unit"))) {
			*p++ = ' ';
			p = gui_cat(p, unit_disp(hf(x, s, "unit")));
		}
	} else if (!st) {
		gui_cat(b, "No reading");
		return 1;
	} else if (kind == K_LIGHT) {
		if (on && hnum(x, s, "bri", &n))
			gui_cat(gui_utoa(b, (u32)clampi(n, 0, 100)), " %");
		else
			gui_cat(b, on ? "On" : "Off");
	} else if (kind == K_CLIMATE) {
		p = gui_cat(b, offline(x) ? "Last: " : "");
		p = gui_cat(p, hvac(hf(x, s, "mode") ? hf(x, s, "mode") : st));
		if (offline(x) && p - b > 6 && b[6] >= 'A' && b[6] <= 'Z')
			b[6] = (char)(b[6] + 32);
		if (val) {
			p = gui_cat(gui_cat(p, " · "), val);
			p = gui_cat(gui_cat(p, " "), unit_disp(hf(x, s, "unit")));
		}
	} else if (kind == K_PRESS) {
		if (a && pnum(a, &n) && n >= 0)
			ago(b, "", n);
		else
			gui_cat(b, "Ready");
	} else if (kind == K_BINARY) {
		gui_cat(b, on ? "Detected" : "Clear");
	} else if (kind == K_PERSON) {
		gui_cat(b, on ? "Home" : "Away");
	} else {
		p = seq(st, "on") ? gui_cat(b, "On") : seq(st, "off") ? gui_cat(b, "Off") : cap(b, st);
	}
	if (w->sub) {
		char t[48];

		if (fmt_value(x, w, w->sub, w->subunit, t)) {
			p = gui_cat(b + slen(b), " · ");
			scopy(p, t, 20);
		}
	}
	return 0;
}

/* entity drawn as a tile: chip, name and state; lights get their brightness as a fill. Narrow tiles (under 140 px) use a
   28 px chip, low ones (under 56 px) one line */
static void draw_tile(const cfg_ctx *x, const cfg_widget *w, int pressed)
{
	const char *s = PS(x->c, w->slot);
	int X = w->x, Y = w->y, W = w->w, H = w->h, cy = Y + H / 2, on = on_state(hf(x, s, "state"));
	int kind = ent_kind(x, w, s), narrow = W < 140, d = narrow ? 28 : 36, isz = narrow ? 20 : 24, tx = X + (narrow ? 42 : 58);
	int role = ent_role(x, s, kind, on), na, bri, ic = ent_icon(x, w, s, kind, on);
	u16 fg, chip, tc;
	char b[48];

	na = ent_state(x, w, s, kind, on, b);
	fg = sc(x, role);
	chip = sc(x, chip_role(role));
	if (kind == K_LIGHT && on > 0) {
		fg = light_col(x, s);
		chip = role == T_AMBER && fg == TK(T_AMBER) ? sc(x, T_CHIP_AMBER) : gui_mix(TK(T_CARD), fg, 3);
	}
	if (na == 2)
		fg = TK(T_DISABLED), chip = TK(T_CHIP_OFF);
	gui_card_c(X, Y, W, H, pressed ? TK(kind == K_LIGHT ? T_PRESSED_AMBER : T_PRESSED) : TK(T_CARD));
	if (kind == K_LIGHT && on > 0 && !pressed && !offline(x) && hnum(x, s, "bri", &bri) && bri > 0) {
		int sv[4];

		gui_clip_in(X + 1, Y + 1, (W - 2) * clampi(bri, 0, 100) / 100, H - 2, sv);
		gui_rrect(X + 1, Y + 1, W - 2, H - 2, 11, TK(T_LIGHT_FILL));
		gui_clip_out(sv);
	}
	gui_chip(X + (narrow ? 22 : 30), cy, d, chip, ic, isz, fg);
	tc = na == 2 ? TK(T_DISABLED) : TK(T_TEXT);
	if (H < 56) {
		int r = gui_tr(GUI_T12, X + W - 8, cy + 5, b, na ? TK(T_TEXT2) : tc);

		gui_tfit(GUI_T14, tx, cy + 5, r - tx - 8, ent_name(x, w), tc);
		return;
	}
	gui_tfit(GUI_T14, tx, cy - 3, X + W - 8 - tx, ent_name(x, w), tc);
	gui_tfit(GUI_T12, tx, cy + 14, X + W - 8 - tx, b, na == 1 || pending(x, s) ? TK(T_TEXT2) : tc);
}

/* read-only entities next to each other on one row share one card (a sensor strip) */
static int ro_entity(const cfg_widget *w)
{
	return w->type == W_ENTITY && !w->action && !w->hold;
}

static int strip_next(const cfg_conf *c, int i, int dir)
{
	const cfg_widget *a = &c->w[i];
	int k;

	for (k = 0; k < c->nwidgets; k++) {
		const cfg_widget *b = &c->w[k];
		int gap = dir > 0 ? b->x - (a->x + a->w) : a->x - (b->x + b->w);

		if (k != i && ro_entity(b) && b->page == a->page && b->y == a->y && b->h == a->h && gap >= 0 && gap <= GAP)
			return k;
	}
	return -1;
}

static void draw_strip_card(const cfg_ctx *x, int i)
{
	const cfg_widget *w = &x->c->w[i];
	int k = i, r;

	while ((r = strip_next(x->c, k, 1)) >= 0)
		k = r;
	gui_card(w->x, w->y, x->c->w[k].x + x->c->w[k].w - w->x, w->h);
}

/* one reading of a strip: 20 px icon, value, and the name under it when the cell is 56 px or higher */
static void draw_reading(const cfg_ctx *x, int idx)
{
	const cfg_widget *w = &x->c->w[idx];
	const char *s = PS(x->c, w->slot);
	int X = w->x, Y = w->y, W = w->w, H = w->h, cy = Y + H / 2, on = on_state(hf(x, s, "state")), n, l;
	int kind = ent_kind(x, w, s), role = ent_role(x, s, kind, on), na;
	char b[48];

	na = ent_state(x, w, s, kind, on, b);
	if (kind == K_SENSOR && hnum(x, s, "val", &n))
		role = thr(w, n, role);
	if (kind == K_BINARY && on > 0)
		role = T_AMBER;
	if ((l = strip_next(x->c, idx, -1)) >= 0)
		gui_vline((x->c->w[l].x + x->c->w[l].w + X) / 2, Y + H / 4, H / 2, TK(T_BORDER));
	gui_icon(ent_icon(x, w, s, kind, on), 20, X + 12, cy - 10, na == 2 ? TK(T_DISABLED) : sc(x, role));
	if (H >= 56) {
		gui_tfit(GUI_T14, X + 36, cy - 1, W - 37, b, na ? TK(na == 2 ? T_DISABLED : T_TEXT2) :
			 role == T_ERROR ? TK(T_ERROR) : offline(x) ? TK(T_TEXT2) : TK(T_TEXT));
		gui_tfit(GUI_T12, X + 36, cy + 15, W - 37, ent_name(x, w), TK(T_TEXT2));
	} else {
		gui_tfit(GUI_T14, X + 36, cy + 5, W - 37, b, na ? TK(T_TEXT2) : offline(x) ? TK(T_TEXT2) : TK(T_TEXT));
	}
}

/* lightctl: on a box lower than 206 px the compact form (chip, name and brightness left, slider right); else the full
   control. Rows of the full form: y of brightness (0 = dim=0), colour temperature and palette (0 = not supported). With
   ct_min/ct_max/color/dim in the config those decide, else the keys the states file has for the slot */
#define LC_FULL(w) ((w)->h >= 206)
#define LC_TX 36	/* full form: tracks start 36 px in, after their 20 px icon */
#define LC_SW 7		/* swatches */

static int lc_split(const cfg_widget *w)
{
	return w->caps & CAP_NODIM ? w->w : w->w * 9 / 16;
}

static void lc_rows(const cfg_ctx *x, const cfg_widget *w, int *r)
{
	const char *s = PS(x->c, w->slot);
	int y = w->y + 48, set = w->caps & CAP_SET;

	r[0] = w->caps & CAP_NODIM ? 0 : y;
	y += r[0] ? 48 : 0;
	r[1] = (set ? w->caps & CAP_CT : hf(x, s, "ct") != 0) ? y : 0;
	y += r[1] ? 48 : 0;
	r[2] = (set ? w->caps & CAP_HUE : hf(x, s, "hue") != 0) ? y : 0;
}

static int ct_lo(const cfg_widget *w) { return w->ctmin ? w->ctmin : 2200; }
static int ct_hi(const cfg_widget *w) { return w->ctmax > ct_lo(w) ? w->ctmax : 6500; }

/* swatches: warm white, cool white, red, orange, green, blue, purple (docs/ui-mockups/light-detail.png) */
static const u16 sw_hue[LC_SW] = { 30, 240, 4, 36, 125, 207, 271 };
static const u8 sw_sat[LC_SW] = { 46, 5, 77, 100, 57, 87, 45 };

/* part under a touch: 2 on/off, 3 brightness, 4 colour temperature, 5..11 colour swatch, 17 hue strip, 18 name area of the
   compact form (detail= page, else toggle), 19 More / Colours (swatches or hue strip) */
static int lc_part(const cfg_ctx *x, const cfg_widget *w, int px, int py)
{
	int r[3], R = w->x + w->w;

	if (!LC_FULL(w))
		return px < w->x + lc_split(w) ? 18 : 3;
	lc_rows(x, w, r);
	if (py < w->y + 44)
		return px >= R - 96 ? 2 : 0;
	if (r[0] && py >= r[0] && py < r[0] + 44)
		return 3;
	if (r[1] && py >= r[1] && py < r[1] + 44)
		return 4;
	if (!r[2] || py < r[2] || py >= r[2] + 48)
		return 0;
	if (px >= R - 96)
		return 19;
	if (x->lcmore)
		return px >= w->x + LC_TX - 8 ? 17 : 0;
	px -= w->x + LC_TX - 4;
	return px >= 0 && px < LC_SW * 48 ? 5 + px / 48 : 0;
}

static int part_of(int code, int idx)
{
	return code >= 0 && code < P_NAV && (code & 255) == idx ? code >> 8 : 0;
}

static const char *const hue_name[] = { "Red", "Orange", "Yellow", "Green", "Cyan", "Blue", "Purple", "Pink", "Red" };

static void draw_light(const cfg_ctx *x, int idx)
{
	const cfg_widget *w = &x->c->w[idx];
	const char *s = PS(x->c, w->slot), *st = hf(x, s, "state");
	int r[3], X = w->x, Y = w->y, W = w->w, H = w->h, v = 0, i, on = on_state(st) > 0, lo = ct_lo(w), hi = ct_hi(w);
	int pp = part_of(x->press, idx), bri = hnum(x, s, "bri", &v), na = st && on_state(st) < 0, hue, sat, ct, k;
	u16 lc = light_col(x, s), fg = na ? TK(T_DISABLED) : TK(T_TEXT);
	char b[48], *p;

	v = clampi(v, 0, 100);
	if (!LC_FULL(w)) {
		int L = lc_split(w), cy = Y + H / 2, sh = imin(40, H - 16);

		gui_card_c(X, Y, W, H, pp == 18 ? TK(T_PRESSED_AMBER) : TK(T_CARD));
		gui_chip(X + 26, cy, 32, on ? (lc == TK(T_AMBER) ? TK(T_CHIP_AMBER) : gui_mix(TK(T_CARD), lc, 3)) : TK(T_CHIP_OFF),
			 on ? ICON_LIGHTBULB : ICON_LIGHTBULB_OUTLINE, 20, on ? lc : na ? TK(T_DISABLED) : TK(T_TEXT2));
		if (na)
			gui_cat(b, "Unavailable");
		else if (!st)
			gui_cat(b, "No reading");
		else
			gui_cat(on && bri ? gui_utoa(b, (u32)v) : b, on && bri ? " %" : on ? "On" : "Off");
		gui_tfit(GUI_T14, X + 50, cy - 3, L - 58, ent_name(x, w), fg);
		gui_tfit(GUI_T12, X + 50, cy + 14, L - 58, b, pending(x, s) || !st ? TK(T_TEXT2) : fg);
		if (!(w->caps & CAP_NODIM))
			gui_ctl_slider(X + L, Y + (H - sh) / 2, W - L - 12, sh, on ? v : 0, on ? lc : TK(T_DISABLED));
		return;
	}
	lc_rows(x, w, r);
	/* big brightness, description, power button */
	if (on && bri) {
		p = gui_utoa(b, (u32)v);
		i = gui_tb(GUI_T28, X + 4, Y + 30, b, fg);
		i = gui_tb(GUI_T16, i + 2, Y + 30, "%", TK(T_TEXT2)) + 12;
	} else {
		i = gui_tb(GUI_T20, X + 4, Y + 28, na ? "Unavailable" : on ? "On" : "Off", na ? fg : TK(T_TEXT2)) + 12;
	}
	b[0] = 0;
	if (on && hnum(x, s, "hue", &hue) && hnum(x, s, "sat", &sat) && sat >= 20) {
		gui_cat(b, hue_name[(clampi(hue, 0, 359) + 22) / 45]);
	} else if (on && hnum(x, s, "ct", &ct)) {
		p = gui_cat(b, ct < 3400 ? "Warm white, " : ct < 5000 ? "Neutral white, " : "Cool white, ");
		gui_cat(gui_utoa(p, (u32)ct), " K");
	}
	gui_tfit(GUI_T12, i, Y + 30, X + W - 72 - i, b, TK(T_TEXT2));
	gui_rrect(X + W - 56, Y, 56, 40, 10, pp == 2 ? TK(T_PRESSED) : on ? gui_mix(TK(T_CARD), lc, 3) : TK(T_CHIP_OFF));
	gui_icon(ICON_POWER, 24, X + W - 40, Y + 8, on ? lc : TK(T_TEXT2));
	if (r[0]) {
		gui_icon(ICON_BRIGHTNESS_6, 20, X + 4, r[0] + 10, TK(T_TEXT2));
		gui_ctl_slider(X + LC_TX, r[0], W - LC_TX, 40, on ? v : 0, on ? lc : TK(T_DISABLED));
	}
	if (r[1]) {
		ct = lo;
		hnum(x, s, "ct", &ct);
		ct = clampi(ct, lo, hi);
		gui_icon(ICON_WHITE_BALANCE_SUNNY, 20, X + 4, r[1] + 10, TK(T_TEXT2));
		gui_ct_track(X + LC_TX, r[1], W - LC_TX, 40, lo, hi);
		gui_handle(X + LC_TX + 4 + (int)gui_udiv((u32)(ct - lo) * (u32)(W - LC_TX - 12), (u32)(hi - lo)), r[1] + 5, 30);
	}
	if (r[2]) {
		gui_icon(ICON_PALETTE, 20, X + 4, r[2] + 10, TK(T_TEXT2));
		if (x->lcmore) {
			int sw = W - LC_TX - 100, hx;

			for (i = 0; i < sw; i++)
				gui_vline(X + LC_TX + i, r[2], 40, hsv((int)gui_udiv((u32)i * 360, (u32)sw), 100));
			gui_rrect_line(X + LC_TX - 2, r[2] - 2, sw + 4, 44, 10, 2, TK(T_BG));
			hue = 0;
			hnum(x, s, "hue", &hue);
			hx = X + LC_TX + (int)gui_udiv((u32)clampi(hue, 0, 359) * (u32)sw, 360);
			gui_handle(clampi(hx - 2, X + LC_TX + 2, X + LC_TX + sw - 6), r[2] + 5, 30);
		} else {
			hue = sat = ct = -1;
			hnum(x, s, "hue", &hue);
			hnum(x, s, "sat", &sat);
			hnum(x, s, "ct", &ct);
			for (i = 0; i < LC_SW; i++) {
				int sx = X + LC_TX + i * 48, cur;

				k = hue - sw_hue[i];
				k = k < 0 ? -k : k;
				cur = on && (hue >= 0 ? (k <= 12 || k >= 348) && sat - sw_sat[i] <= 20 && sw_sat[i] - sat <= 20 :
					     ct > 0 && (i == 0 ? ct < 4000 : i == 1 && ct >= 5000));
				if (cur || pp == 5 + i)
					gui_rrect_line(sx - 4, r[2] - 4, 48, 48, 12, 2, pp == 5 + i ? TK(T_TEXT2) : TK(T_TEXT));
				gui_rrect(sx, r[2], 40, 40, 8, hsv(sw_hue[i], sw_sat[i]));
			}
		}
		gui_button(X + W - 92, r[2], 92, 40, x->lcmore ? "Colours" : "More", pp == 19 ? GUI_PRESSED : 0);
	}
	/* footer below the control: when HA last saw a change */
	if (hnum(x, s, "age", &k) && k >= 0 && Y + H + 10 <= GUI_H - 4) {
		gui_unclip();
		ago(b, "Changed ", k);
		gui_tb(GUI_T12, X + 4, GUI_H - 14, b, TK(T_TEXT2));
	}
}

/* thermo parts: 1 minus, 2 plus, 3 mode. Lower than 150 px: the compact form (name, mode and current value on the left,
   tap there for the next mode; minus, target, plus on the right) */
#define TH_BW 62

static int th_part(const cfg_widget *w, int px, int py)
{
	int R = w->x + w->w;

	if (w->h < 150)
		return px < R - 128 ? 3 : px < R - 88 ? 1 : px >= R - 48 ? 2 : 0;
	if (py < w->y + 56 && (w->w < 260 || px >= R - 112))
		return 3;
	if (py < w->y + w->h - 56)
		return 0;
	return px >= R - 78 ? 2 : px >= R - 2 * TH_BW - 24 ? 1 : 0;
}

static void th_button(int x, int y, int w, int icon, int isz, int pressed, u16 fg)
{
	gui_rrect(x, y, w, 40, 10, pressed ? TK(T_PRESSED) : TK(T_CHIP_OFF));
	gui_icon(icon, isz, x + (w - isz) / 2, y + 20 - isz / 2, fg);
}

static void draw_thermo(const cfg_ctx *x, int idx)
{
	const cfg_widget *w = &x->c->w[idx];
	const char *s = PS(x->c, w->slot), *v = hf(x, s, "val"), *m = hf(x, s, "mode"), *tg = hf(x, s, "target");
	int X = w->x, Y = w->y, W = w->w, H = w->h, R = X + W, t, pp = part_of(x->press, idx), cy = Y + H / 2, n;
	int role = m && seq(m, "cool") ? T_COOL : m && seq(m, "heat") ? T_HEAT : T_TEXT2;
	u16 ctl = offline(x) ? TK(T_DISABLED) : TK(T_TEXT), tcol = pending(x, s) || offline(x) ? TK(T_TEXT2) : TK(T_TEXT);
	char b[48], *p;

	if (!m)
		m = hf(x, s, "state");
	if (tg && tenths(tg, &t))
		gui_ftoa(b, t);
	else
		b[0] = 0;
	gui_card(X, Y, W, H);
	if (H < 150) {
		gui_tfit(GUI_T14, X + 12, cy - 3, W - 152, ent_name(x, w), TK(T_TEXT));
		cap(b + 16, m ? m : "No reading");
		n = gui_tb(GUI_T12, X + 12, cy + 14, b + 16, sc(x, role));
		if (v) {
			p = gui_cat(gui_cat(b + 16, " · "), v);
			gui_cat(gui_cat(p, " "), unit_disp(hf(x, s, "unit")));
			gui_tfit(GUI_T12, n, cy + 14, R - 136 - n, b + 16, TK(T_TEXT2));
		}
		th_button(R - 124, cy - 20, 36, ICON_MINUS, 20, pp == 1, ctl);
		th_button(R - 48, cy - 20, 36, ICON_PLUS, 20, pp == 2, ctl);
		gui_tb(GUI_T14, R - 68 - gui_text_w(GUI_T14, b[0] ? b : "--") / 2, cy + 5, b[0] ? b : "--", tcol);
		return;
	}
	gui_chip(X + 30, Y + 36, 36, sc(x, chip_role(role)), role == T_COOL ? ICON_SNOWFLAKE : ICON_RADIATOR, 24,
		 sc(x, role));
	gui_tfit(GUI_T14, X + 58, Y + 33, (W >= 260 ? R - 112 : R - 8) - X - 58, ent_name(x, w), TK(T_TEXT));
	p = gui_cat(b + 16, offline(x) ? "Last: " : "");
	cap(p, m ? hvac(m) : "No reading");
	if (offline(x) && p[0] >= 'A' && p[0] <= 'Z')
		p[0] = (char)(p[0] + 32);
	gui_tfit(GUI_T12, X + 58, Y + 50, (W >= 260 ? R - 112 : R - 8) - X - 58, b + 16, TK(T_TEXT));
	if (W >= 260) {
		cap(b + 16, m ? m : "Mode");
		gui_button(R - 100, Y + 16, 88, 40, b + 16, (pp == 3 ? GUI_PRESSED : 0) | (offline(x) ? GUI_OFF : 0));
	}
	gui_tb(GUI_T12, X + 12, Y + 88, "Target", TK(T_TEXT2));
	if (b[0]) {
		n = gui_tb(GUI_T28, X + 11, Y + 120, b, tcol);
		gui_tb(GUI_T14, n + 2, Y + 120, "°C", TK(T_TEXT2));
	} else {
		gui_tb(GUI_T14, X + 12, Y + 120, "No reading", TK(T_TEXT2));
	}
	if (v) {
		p = gui_cat(gui_cat(b + 16, "Now "), v);
		gui_cat(gui_cat(p, " "), unit_disp(hf(x, s, "unit")));
		gui_tb(GUI_T12, X + 12, Y + 140, b + 16, TK(T_TEXT2));
	}
	th_button(R - 2 * TH_BW - 20, Y + H - 52, TH_BW, ICON_MINUS, 24, pp == 1, ctl);
	th_button(R - TH_BW - 12, Y + H - 52, TH_BW, ICON_PLUS, 24, pp == 2, ctl);
}

/* the list whose card holds this widget (row-style buttons and toggles inside it draw no card of their own), or -1 */
static int in_list(const cfg_conf *c, const cfg_widget *w)
{
	int i;

	for (i = 0; i < c->nwidgets; i++) {
		const cfg_widget *l = &c->w[i];

		if (l != w && l->type == W_LIST && l->page == w->page && w->x >= l->x && w->y >= l->y &&
		    w->x + w->w <= l->x + l->w && w->y + w->h <= l->y + l->h)
			return i;
	}
	return -1;
}

/* a list-style row: optional 24 px icon, title, optional second line; returns the x where trailing content must end */
static void row_text(const cfg_ctx *x, const cfg_widget *w, const char *title, const char *line2, int right, int dis)
{
	int cy = w->y + w->h / 2, tx = w->x + (w->icon ? 48 : 12), l = in_list(x->c, w);

	if (l >= 0 && w->y > x->c->w[l].y + 2)
		gui_hline(tx, w->y - 1, x->c->w[l].x + x->c->w[l].w - 1 - tx, TK(T_BORDER));
	if (w->icon)
		gui_icon(w->icon - 1, 24, w->x + 12, cy - 12, TK(dis ? T_DISABLED : T_TEXT2));
	if (line2 && *line2) {
		gui_tfit(GUI_T14, tx, cy - 3, right - tx - 8, title, TK(dis ? T_DISABLED : T_TEXT));
		gui_tfit(GUI_T12, tx, cy + 14, right - tx - 8, line2, TK(T_TEXT2));
	} else {
		gui_tfit(GUI_T14, tx, cy + 5, right - tx - 8, title, TK(dis ? T_DISABLED : T_TEXT));
	}
}

static const gui_font *wfont(const cfg_widget *w, const gui_font *def)
{
	return w->font == 1 ? GUI_T12 : w->font == 2 ? GUI_T28 : def;
}

/* storage, restart and power-off: red labels and a confirmation that says what happens */
static int danger(const cfg_ctx *x, const cfg_widget *w)
{
	return w->action == A_STORAGE || (w->action == A_CMD && cmd_find(PS(x->c, w->arg)) >= CMD_RESTART);
}

static void draw_widget(const cfg_ctx *x, int idx)
{
	const cfg_conf *c = x->c;
	const cfg_widget *w = &c->w[idx];
	int X = w->x, Y = w->y, W = w->w, H = w->h, known, n, pressed = x->press == idx, R = X + W, cy = Y + H / 2, role;
	char b[48], b2[48];
	const char *lab = PS(c, w->label);
	const gui_font *f;

	gui_clip(X, Y, W, H);
	switch (w->type) {
	case W_LABEL:
		f = wfont(w, GUI_T14);
		if (w->key) {
			fmt_value(x, w, w->key, w->unit, b);
			lab = b;
		}
		if (w->align == 1)
			gui_text_c(f, X, Y, W, lab, TK(col_role[w->color]));
		else if (w->align == 2)
			gui_text_r(f, X, Y, W, lab, TK(col_role[w->color]));
		else
			gui_text(f, X, Y, lab, TK(col_role[w->color]));
		break;
	case W_TILE:
	case W_BAR:
	case W_GAUGE: {
		int na = not_avail(x, w), big = H >= 72, vb;

		gui_card_c(X, Y, W, H, pressed ? TK(T_PRESSED) : TK(T_CARD));
		known = fmt_value(x, w, w->key, w->unit, b);
		role = na ? T_DISABLED : value_role(x, w, known);
		b2[0] = 0;
		if (w->sub)
			fmt_value(x, w, w->sub, w->subunit, b2);
		if (w->type == W_GAUGE) {
			int r = imin((W - 28) / 2, (H - 40) * 10 / 17), gx = X + W / 2, gy = Y + 34 + r;

			gui_tfit(GUI_T12, X + 12, Y + 22, W - 24, lab, TK(T_TEXT2));
			n = 0;
			if (known)
				numval(x, PS(c, w->key), &n);
			n = known ? pct_of(n, w->min, w->max) : 0;
			arc(gx, gy, r - 4, 135, 405, 64, TK(T_CHIP_OFF));
			if (n > 0)
				arc(gx, gy, r - 4, 135, 135 + (n * 270 / 100) / 5 * 5, 64, role == T_ERROR ? TK(T_ERROR) : TK(T_STEEL));
			if (known)
				gui_tb(GUI_T20, gx - gui_text_w(GUI_T20, b) / 2, gy + 7, b, TK(role == T_TEXT2 ? T_TEXT : role));
			else
				gui_tb(GUI_T12, gx - gui_text_w(GUI_T12, "No reading") / 2, gy + 4, "No reading", TK(T_TEXT2));
			if (b2[0])
				gui_tb(GUI_T12, gx - gui_text_w(GUI_T12, b2) / 2, gy + 24, b2, TK(T_TEXT2));
			break;
		}
		gui_tfit(GUI_T12, X + 12, Y + 22, W - 24, lab, TK(na ? T_DISABLED : T_TEXT2));
		vb = big ? Y + 52 : Y + 42;
		if (na)
			gui_tfit(GUI_T14, X + 12, vb, W - 24, "Unavailable", TK(T_DISABLED));
		else if (!known)
			gui_tb(GUI_T12, X + 12, vb, "No reading", TK(T_TEXT2));
		else
			gui_tfit(wfont(w, big ? GUI_T20 : GUI_T14), X + 12, w->font == 2 ? Y + 62 : vb, W - 24, b,
				 TK(role == T_TEXT2 ? T_TEXT : role));
		if (b2[0]) {
			if (big && vb + 22 <= Y + H - 6 && w->type == W_TILE)
				gui_tfit(GUI_T12, X + 12, vb + 20, W - 24, b2, TK(T_TEXT2));
			else
				gui_tr(GUI_T12, R - 12, Y + 22, b2, TK(T_TEXT2));
		}
		if (w->type == W_BAR) {
			n = 0;
			if (known)
				numval(x, PS(c, w->key), &n);
			gui_progress(X + 12, Y + H - 20, W - 24, 8, known && !na ? pct_of(n, w->min, w->max) : 0,
				     role == T_ERROR ? TK(T_ERROR) : TK(T_STEEL));
		}
		break;
	}
	case W_BATTERY: {
		int pct = -1, chg = 0, ext = 0;
		const char *st;

		gui_card(X, Y, W, H);
		gui_tb(GUI_T12, X + 12, Y + 22, w->label ? lab : "Battery", TK(T_TEXT2));
		known = numval(x, PS(c, w->key), &pct);
		if (!known)
			pct = -1;
		numval(x, PS(c, w->sub), &chg);
		numval(x, PS(c, w->colorkey), &ext);
		st = chg ? "Charging" : ext ? "External power" : "On battery";
		if (pct >= 0) {
			gui_utoa(b, (u32)clampi(pct, 0, 100));
			n = gui_tb(H >= 140 ? GUI_T28 : GUI_T20, X + 12, Y + (H >= 140 ? 62 : 50), b,
				   TK(pct > 15 ? T_TEXT : T_ERROR));
			gui_tb(GUI_T14, n + 2, Y + (H >= 140 ? 62 : 50), "%", TK(T_TEXT2));
		} else {
			gui_tb(GUI_T12, X + 12, Y + 50, "No reading", TK(T_TEXT2));
		}
		if (H >= 140) {
			gui_progress(X + 12, Y + H - 52, W - 24, 8, pct, TK(pct > 15 ? T_STEEL : T_ERROR));
			gui_icon(chg ? ICON_BATTERY_CHARGING : ext ? ICON_POWER_PLUG : ICON_BATTERY, 20, X + 12, Y + H - 34,
				 TK(T_TEXT2));
			gui_tb(GUI_T12, X + 38, Y + H - 19, st, TK(T_TEXT2));
		} else {
			gui_tr(GUI_T12, R - 12, Y + 22, st, TK(T_TEXT2));
			gui_progress(X + 12, Y + H - 20, W - 24, 8, pct, TK(pct > 15 ? T_STEEL : T_ERROR));
		}
		break;
	}
	case W_BUTTON: {
		int fl = (is_active(x, w) ? GUI_ACTIVE : 0) | (pressed ? GUI_PRESSED : 0) |
			 (danger(x, w) ? GUI_DANGER : 0) | (w->action == A_HA && offline(x) ? GUI_OFF : 0);

		if (w->key)
			fmt_value(x, w, w->key, w->unit, b);
		else
			b[0] = 0;
		if (w->verb) {
			/* row: icon, title, status, trailing text button; the whole row is the target */
			const char *v = PS(c, w->verb);
			int bw = imax(56, gui_text_w(GUI_T14, v) + 24);

			if (in_list(c, w) < 0)
				gui_card(X, Y, W, H);
			row_text(x, w, lab, b[0] ? b : w->key ? "No reading" : PS(c, w->unit), R - 10 - bw, 0);
			gui_button(R - 10 - bw, cy - 19, bw, 38, v, fl & ~GUI_ACTIVE);
		} else if (w->icon && H >= 72) {
			/* tile that opens a page or runs a command: chip, title, status, chevron for pages */
			gui_card_c(X, Y, W, H, pressed ? TK(T_PRESSED) : TK(T_CARD));
			gui_chip(X + 30, Y + 30, 36, (fl & (GUI_DANGER | GUI_OFF)) ? TK(T_CHIP_OFF) : TK(T_CHIP_PRIMARY), w->icon - 1, 24,
				 (fl & GUI_DANGER) ? TK(T_ERROR) : (fl & GUI_OFF) ? TK(T_DISABLED) : TK(T_PRIMARY));
			if (w->action == A_PAGE)
				gui_icon(ICON_CHEVRON_RIGHT, 20, R - 28, Y + 20, TK(T_TEXT2));
			gui_tfit(GUI_T14, X + 12, Y + H - (b[0] ? 30 : 16), W - 24, lab, TK(T_TEXT));
			if (b[0])
				gui_tfit(GUI_T12, X + 12, Y + H - 12, W - 24, b, TK(T_TEXT2));
		} else {
			gui_button(X, Y, W, H, lab, fl);
		}
		break;
	}
	case W_SLIDER: {
		int v = w->var >= 0 ? x->var[w->var].value : w->min, sh = imin(40, H - 48);

		gui_card(X, Y, W, H);
		gui_tfit(GUI_T14, X + 12, Y + 28, W - 80, lab, TK(T_TEXT));
		gui_cat(itoa_s(b, v), PS(c, w->unit));
		gui_tr(GUI_T14, R - 12, Y + 28, b, TK(T_TEXT2));
		gui_slider(X + 12, Y + H - 12 - sh, W - 24, sh, pct_of(v, w->min, w->max), 0);
		break;
	}
	case W_TOGGLE: {
		int on = is_active(x, w);

		if (in_list(c, w) < 0)
			gui_card_c(X, Y, W, H, pressed ? TK(T_PRESSED) : TK(T_CARD));
		else if (pressed)
			gui_rect(X + 1, Y, W - 2, H, TK(T_PRESSED));
		if (w->key)
			fmt_value(x, w, w->key, w->unit, b);
		else
			b[0] = 0;
		row_text(x, w, lab, b, R - 56, 0);
		gui_switch(R - 56, cy - 12, on, 0);
		break;
	}
	case W_LIST:
		gui_card(X, Y, W, H);
		if (lab[0])
			gui_tfit(GUI_T14, X + 12, Y + 24, W - 24, lab, TK(T_TEXT));
		break;
	case W_ROW: {
		int right = R - (w->action == A_PAGE || w->action == A_TZ ? 36 : 12), na = not_avail(x, w);
		const cfg_widget *l = &c->w[idx];

		while (l > c->w && l->type == W_ROW)
			l--;
		gui_clip(l->x, l->y, l->w, l->h);
		if (pressed)
			gui_rect(X + 1, Y, W - 2, H, TK(T_PRESSED));
		if (w->y > l->y + 1)
			gui_hline(X + (w->icon ? 48 : 12), Y, W - (w->icon ? 49 : 13), TK(T_BORDER));
		if (w->icon)
			gui_icon(w->icon - 1, 24, X + 12, cy - 12, TK(T_TEXT2));
		known = w->key ? fmt_value(x, w, w->key, w->unit, b) : 0;
		if (w->action == A_PAGE)
			gui_icon(ICON_CHEVRON_RIGHT, 20, R - 30, cy - 10, TK(T_TEXT2));
		if (w->action == A_TZ && is_active(x, w))
			gui_icon(ICON_CHECK, 20, R - 30, cy - 10, TK(T_PRIMARY));
		if (na)
			n = gui_tr(GUI_T14, right, cy + 5, "Unavailable", TK(T_DISABLED));
		else if (known)
			n = gui_tr(GUI_T14, right, cy + 5, b, TK(value_role(x, w, known) == T_TEXT2 ? T_TEXT : value_role(x, w, known)));
		else if (w->key)
			n = gui_tr(GUI_T12, right, cy + 5, "No reading", TK(T_TEXT2));
		else
			n = right;
		gui_tfit(GUI_T14, X + (w->icon ? 48 : 12), cy + 5, n - X - (w->icon ? 48 : 12) - 12,
			 PS(c, w->label ? w->label : w->key), TK(na ? T_DISABLED : T_TEXT));
		break;
	}
	case W_ENTITY:
		if (ro_entity(w))
			draw_reading(x, idx);
		else
			draw_tile(x, w, pressed);
		break;
	case W_LIGHTCTL:
		draw_light(x, idx);
		break;
	case W_THERMO:
		draw_thermo(x, idx);
		break;
	default:
		break;
	}
	gui_unclip();
}

/* ---------- header, dots, dialog ---------- */

#define WM_X 35				/* wordmark pen; baseline 20 puts its x-height centre on the mark's axis (15.5) */
#define HDR_TITLE_X (WM_X + gui_wordmark_adv + 17)	/* separator 7 px after the word, title 10 px after that */
#define TAB_PAD 8

static int page_dots(const cfg_ctx *x)
{
	return dot_count(x->c, x->page);
}

/* the header shows a back arrow on hidden pages, and on the hub when there is an area page to go back to */
static int has_back(const cfg_ctx *x)
{
	const cfg_page *p = &x->c->page[x->page];

	return p->hidden || (p->hub && x->c->nvis > 0);
}

static int has_gear(const cfg_ctx *x)
{
	const cfg_page *p = &x->c->page[x->page];

	return x->c->hub >= 0 && !p->hidden && !p->hub;
}

/* header tabs (nav=tabs/both): the area pages as text tabs after the title separator, when 2..5 of them fit before x 300 */
static int nav_geom(const cfg_ctx *x, int i, int *bx, int *bw)
{
	const cfg_conf *c = x->c;
	int k, px = HDR_TITLE_X - TAB_PAD, me = -1;

	if (!(c->nav & NAV_TABS) || c->nvis < 2 || c->nvis > 5 || has_back(x) || c->page[x->page].hub)
		return 0;
	for (k = 0; k < c->npages; k++) {
		const cfg_page *p = &c->page[k];
		int w;

		if (p->hidden || p->hub)
			continue;
		w = gui_text_w(GUI_T14, PS(c, p->title)) + 2 * TAB_PAD;
		if (k == i) {
			*bx = px;
			*bw = w;
			me = k;
		}
		px += w;
	}
	return px <= 300 && me >= 0;
}

/* x of the dot strip's first dot and the count */
static int dots_x(const cfg_ctx *x, int *n)
{
	*n = page_dots(x);
	return GUI_W / 2 - (*n - 1) * 6;
}

static void dots(const cfg_ctx *x)
{
	int i, n, me = 0, k = 0, x0 = dots_x(x, &n);

	if (!n)
		return;
	for (i = 0; i < x->c->npages; i++)
		if (peer(x->c, x->page, i)) {
			if (i == x->page)
				me = k;
			k++;
		}
	for (i = 0; i < n; i++)
		gui_circle(x0 + i * 12, 260, 3, i == me ? TK(T_TEXT) : TK(T_DOT_OFF));
}

static void dim_screen(void)
{
	int x, y;

	for (y = 0; y < GUI_H; y++)
		for (x = 0; x < GUI_W; x++)
			gui_blend(x, y, TK(T_BG), 11);
}

#define CF_X 70
#define CF_Y 56
#define CF_W 340
#define CF_H 160

static const char *const cf_text[3][4] = {
	{ "Restart in storage mode?", "The screen stops. With USB attached the Mac sees", "INTERNAL; eject it to start Linux again.",
	  "Restart" },
	{ "Restart the device?", "It restarts through the storage window and comes", "back in about a minute.", "Restart" },
	{ "Power off?", "The unit stays off until the power key is pressed.", "Home Assistant shows it offline.", "Power off" },
};

static void draw_confirm(const cfg_ctx *x)
{
	const cfg_widget *w = &x->c->w[x->confirm];
	char b[64];
	int k = w->action == A_STORAGE ? 0 : w->action == A_CMD ? cmd_find(PS(x->c, w->arg)) - CMD_RESTART + 1 : -1;

	dim_screen();
	gui_card(CF_X, CF_Y, CF_W, CF_H);
	if (k >= 0 && k < 3) {
		gui_tb(GUI_T16, CF_X + 20, CF_Y + 34, cf_text[k][0], TK(T_TEXT));
		gui_tb(GUI_T12, CF_X + 20, CF_Y + 58, cf_text[k][1], TK(T_TEXT2));
		gui_tb(GUI_T12, CF_X + 20, CF_Y + 74, cf_text[k][2], TK(T_TEXT2));
	} else {
		k = -1;
		scopy(b, PS(x->c, w->label ? w->label : w->slot), 40);
		gui_cat(b + slen(b), "?");
		gui_tfit(GUI_T16, CF_X + 20, CF_Y + 34, CF_W - 40, b, TK(T_TEXT));
	}
	gui_button(CF_X + 16, CF_Y + CF_H - 56, 146, 40, "Cancel", x->press == P_CANCEL ? GUI_PRESSED : 0);
	gui_button(CF_X + CF_W - 162, CF_Y + CF_H - 56, 146, 40, k >= 0 ? cf_text[k][3] : "OK",
		   (k >= 0 ? GUI_DANGER : GUI_ACTIVE) | (x->press == P_OK ? GUI_PRESSED : 0));
}

/* power key hold hint: a card in the middle of the screen, over everything */
static void draw_hint(const cfg_ctx *x)
{
	int ten = x->ovl == 10;
	u16 c = ten ? TK(T_ERROR) : TK(T_AMBER);

	dim_screen();
	gui_card(CF_X, 86, CF_W, 100);
	gui_chip(CF_X + 42, 136, 44, ten ? TK(T_CHIP_OFF) : TK(T_CHIP_AMBER), ten ? ICON_RESTART : ICON_POWER, 24, c);
	gui_tb(GUI_T16, CF_X + 78, 132, ten ? "Restarting…" : "Release to power off", c);
	gui_tb(GUI_T12, CF_X + 78, 152, ten ? "Release the power key." : "Keep holding to restart instead.", TK(T_TEXT2));
}

/* header, 32 px: mark and wordmark (or a back arrow and the page title on subpages), the title or tabs, and on the right
   the gear (area pages), the battery, the clock; a note or the offline state takes the clock's place */
static void header(const cfg_ctx *x)
{
	const cfg_conf *c = x->c;
	const cfg_page *p = &c->page[x->page];
	char b[48];
	const char *v;
	int rx = 468, lx, i, bx, bw, pct = -1, chg = 0, ext = 0, tabs = 0;

	if (has_gear(x)) {
		gui_icon(ICON_COG, 24, 444, 4, x->press == P_GEAR ? TK(T_TEXT) : TK(T_TEXT2));
		rx = 432;
	}
	if (!numval(x, "battery_pct_est", &pct))
		pct = -1;
	if (!numval(x, "charging", &chg) || !rawval(x, "charging"))
		numval(x, "external_power", &ext);
	rx = gui_batt_ind(rx, 21, pct, chg || ext) - 12;
	if (x->note_ttl) {
		lx = gui_tr(GUI_T12, rx, 21, x->note, TK(T_WARNING));
	} else if (offline(x)) {
		if (x->hage < 0) {
			gui_cat(b, "Offline");
		} else {
			gui_cat(gui_utoa(gui_cat(b, "Offline "), (u32)x->hage / 60), " min");
		}
		lx = gui_tr(GUI_T12, rx, 21, b, TK(T_WARNING)) - 24;
		gui_icon(ICON_CLOUD_OFF_OUTLINE, 20, lx, 6, TK(T_WARNING));
	} else if ((v = time_key(x, "clock", b))) {
		lx = gui_tr(GUI_T14, rx, 21, v, TK(T_TEXT2));
	} else {
		lx = rx + 12;
	}
	if (has_back(x)) {
		gui_icon(ICON_ARROW_LEFT, 24, 12, 4, x->press == P_BACK ? TK(T_TEXT2) : TK(T_TEXT));
		gui_tfit(GUI_T16, 48, 22, lx - 60, PS(c, p->title), TK(T_TEXT));
		return;
	}
	gui_mark(12, 7, 18, TK(T_MARK));
	gui_wordmark(WM_X, 20, TK(T_BONE));
	gui_vline(HDR_TITLE_X - 10, 9, 14, TK(T_BORDER));
	for (i = 0; i < c->npages; i++)
		if (nav_geom(x, i, &bx, &bw)) {
			tabs = 1;
			gui_tb(GUI_T14, bx + TAB_PAD, 21, PS(c, c->page[i].title), i == x->page ? TK(T_TEXT) : TK(T_TEXT2));
			if (i == x->page)
				gui_rect(bx + TAB_PAD, 29, bw - 2 * TAB_PAD, 2, TK(T_PRIMARY));
		}
	if (!tabs)
		gui_tfit(GUI_T16, HDR_TITLE_X, 22, lx - 12 - HDR_TITLE_X, PS(c, p->title), TK(T_TEXT));
}

void cfg_draw(cfg_ctx *x)
{
	const cfg_conf *c = x->c;
	int i;

	gui_theme(c->theme);
	gui_unclip();
	gui_clear(TK(T_BG));
	header(x);
	for (i = 0; i < c->nwidgets; i++)
		if (c->w[i].page == x->page && ro_entity(&c->w[i]) && strip_next(c, i, -1) < 0)
			draw_strip_card(x, i);
	for (i = 0; i < c->nwidgets; i++)
		if (c->w[i].page == x->page && c->w[i].type != W_ON)
			draw_widget(x, i);
	dots(x);
	if (x->confirm >= 0)
		draw_confirm(x);
	if (x->ovl)
		draw_hint(x);
	if (x->note_ttl)
		x->note_ttl--;
}

/* ---------- actions ---------- */

static void emit_s(cfg_ctx *x, const char *ev, const char *name, const char *val)
{
	char b[128], *p = b;

	p = gui_cat(p, "seq=");
	p = gui_utoa(p, ++x->seq);
	p = gui_cat(p, " up=");
	p = gui_utoa(p, x->st.up);
	p = gui_cat(p, " event=");
	p = gui_cat(p, ev);
	p = gui_cat(p, " name=");
	p = gui_cat(p, name);
	p = gui_cat(p, " value=");
	p = gui_cat(p, val);
	p = gui_cat(p, " page=");
	p = gui_cat(p, PS(x->c, x->c->page[x->page].id));
	p = gui_cat(p, "\n");
	plat_event(b, (int)(p - b));
}

void cfg_emit(cfg_ctx *x, const char *ev, const char *name, int val)
{
	char b[12];

	itoa_s(b, val);
	emit_s(x, ev, name, b);
}

static void opt_set(cfg_ctx *x, const char *slot, const char *field, const char *v)
{
	char k[40];
	int i, f = 0;

	ha_key(x, slot, field, k);
	for (i = 0; i < CFG_HA_OPT; i++) {
		if (seq(x->opt[i].k, k))
			break;
		if (x->opt[f].k[0] && (!x->opt[i].k[0] || (int)(x->opt[i].until - x->opt[f].until) < 0))
			f = i;	/* a free entry, else the oldest */
	}
	if (i == CFG_HA_OPT)
		i = f;
	scopy(x->opt[i].k, k, sizeof(x->opt[i].k));
	scopy(x->opt[i].v, v, sizeof(x->opt[i].v));
	x->opt[i].until = x->now + CFG_OPT_MS;
}

/* show the expected result of an HA operation until the states file confirms it (or CFG_OPT_MS passed) */
static void ha_opt(cfg_ctx *x, const char *slot, const char *op)
{
	const char *a = op;
	char h[8];
	int i;

	while (*a && *a != ':')
		a++;
	if (*a)
		a++;
	if (seq(op, "toggle")) {
		i = on_state(hf(x, slot, "state"));
		if (i >= 0)
			opt_set(x, slot, "state", i ? "off" : "on");
	} else if (prefix(op, "set_bri:")) {
		opt_set(x, slot, "bri", a);
		opt_set(x, slot, "state", seq(a, "0") ? "off" : "on");
	} else if (prefix(op, "set_hue_sat:")) {
		for (i = 0; a[i] && a[i] != ',' && i < 7; i++)
			h[i] = a[i];
		h[i] = 0;
		opt_set(x, slot, "hue", h);
		opt_set(x, slot, "sat", a + i + (a[i] == ','));
	} else if (!seq(op, "press")) {
		opt_set(x, slot, op[0] == 'm' ? "mode" : op[4] == 'c' ? "ct" : "target", a);
	}
}

/* send now: one event=ha line */
static void ha_do(cfg_ctx *x, const char *slot, const char *op)
{
	if (slot[0] == '*')
		slot = x->dslot;
	if (!slot[0])
		return;
	ha_opt(x, slot, op);
	emit_s(x, "ha", slot, op);
	scopy(x->lastop, op, sizeof(x->lastop));
	x->last_send = x->now;
}

/* same operation name (the part before ':') */
static int op_same(const char *a, const char *b)
{
	while (*a && *a != ':' && *a == *b)
		a++, b++;
	return (!*a || *a == ':') && (!*b || *b == ':');
}

/* drags: send at once when the last send is CFG_THROTTLE_MS old, else when it is; idle: CFG_IDLE_MS after the last call.
   A newer value replaces the waiting one; a different slot or operation sends the waiting one first */
static void ha_queue(cfg_ctx *x, const char *slot, const char *op, int idle)
{
	if (slot[0] == '*')
		slot = x->dslot;
	if (!slot[0])
		return;
	if (x->pend && (!seq(x->pslot, slot) || !op_same(x->pop, op)))
		ha_do(x, x->pslot, x->pop);
	x->pend = 0;
	if (!idle && (int)(x->now - x->last_send) >= CFG_THROTTLE_MS) {
		ha_do(x, slot, op);
		return;
	}
	ha_opt(x, slot, op);
	scopy(x->pslot, slot, sizeof(x->pslot));
	scopy(x->pop, op, sizeof(x->pop));
	x->pend = 1;
	x->due = idle ? x->now + CFG_IDLE_MS : x->last_send + CFG_THROTTLE_MS;
}

/* the neighbour after (dir 1) or before (dir -1) the current page; -1 at either end unless wrap */
static int page_walk(const cfg_ctx *x, int dir, int wrap)
{
	int a = x->page, k, n = x->c->npages;

	for (k = 1; k < n; k++) {
		a += dir;
		if (a < 0 || a >= n) {
			if (!wrap)
				return -1;
			a = a < 0 ? n - 1 : 0;
		}
		if (peer(x->c, x->page, a))
			return a;
	}
	return -1;
}

static void set_var(cfg_ctx *x, int i, int v)
{
	if (i < 0)
		return;
	x->var[i].value = v;
	cfg_emit(x, "set", x->var[i].name, v);
}

static int spawn(cfg_ctx *x, int slot, const char *const *argv, const char *what)
{
	if (plat_spawn(slot, argv) < 0) {
		cfg_set_note(x, what, " busy or failed");
		return -1;
	}
	return 0;
}

/* page:cycle from a top-level page: the next area page, after the last one the hub, from the hub the first area */
static int cycle(const cfg_ctx *x)
{
	const cfg_conf *c = x->c;
	int i, first = -1, after = -1, me = c->page[x->page].hub ? c->npages : x->page;

	for (i = 0; i < c->npages; i++)
		if (!c->page[i].hidden && !c->page[i].hub) {
			if (first < 0)
				first = i;
			if (i > me && after < 0)
				after = i;
		}
	if (after >= 0)
		return after;
	if (c->hub >= 0 && !c->page[x->page].hub)
		return c->hub;
	return first >= 0 ? first : x->page;
}

/* page of action=page arg=: an id, next, prev, back (a subpage's parent, from the hub the last area page, else the
   previous page) or cycle (back on a subpage, else cycle()) */
static int page_target(const cfg_ctx *x, const char *arg)
{
	const cfg_page *p = &x->c->page[x->page];
	int a;

	if (seq(arg, "next") || seq(arg, "prev")) {
		if ((a = page_walk(x, arg[0] == 'n' ? 1 : -1, 1)) < 0)
			a = x->page;
	} else if (seq(arg, "back") || (seq(arg, "cycle") && p->hidden)) {
		a = p->parent ? find_page(x->c, PS(x->c, p->parent)) : p->hub && x->c->nvis ? x->area : x->prev;
	} else if (seq(arg, "cycle")) {
		a = cycle(x);
	} else {
		a = find_page(x->c, arg);
	}
	return a < 0 || a >= x->c->npages ? x->c->start : a;
}

void cfg_lock(cfg_ctx *x, int on)
{
	char n[12];
	const char *argv[] = { "/bin/homtom-bl", n, 0 };
	int v = var_get(x, "backlight");

	if (on == x->locked)
		return;
	/* the backlight variable keeps the level to come back to (at least the slider's 5 % floor, 100 % when unset) */
	if (!on && v < BL_MIN)
		v = 100;
	gui_utoa(n, on ? 0 : (u32)clampi(v, BL_MIN, 100) * 255 / 100);
	/* a lock whose backlight command was refused would leave a lit screen that ignores touches: stay unlocked */
	if (spawn(x, SLOT_BACKLIGHT, argv, "backlight") < 0 && on)
		return;
	x->locked = on;
	x->press = x->drag = x->hcand = -1;
	x->swallow = x->down;
	emit_s(x, "screen", on ? "lock" : "unlock", "1");
}

/* tz:set:SETTING writes INTERNAL:/homtom/tz; tz:step:N moves the manual offset (no summer time) by N minutes */
static void tz_action(cfg_ctx *x, const char *arg)
{
	char b[16];
	int off, rule;

	if (prefix(arg, "set:")) {
		scopy(b, arg + 4, sizeof(b));
	} else {
		pnum(arg + 5, &rule);
		if (!(x->tzset[0] && tz_parse(x->tzset, &off, &rule) && rule == 0))
			tz_now(x, &off);
		pnum(arg + 5, &rule);
		itoa_s(b, clampi(off + rule, -720, 840));
	}
	if (plat_tz_write(b) < 0) {
		cfg_set_note(x, "time zone not saved", 0);
		return;
	}
	cfg_tzfile(x, b, slen(b));
	emit_s(x, "tz", b, "1");
}

/* action `act` with `arg` of widget idx (tap, hold or power key) */
static void act(cfg_ctx *x, int idx, int act, const char *arg, int v, int have_v)
{
	cfg_widget *w = &x->c->w[idx];
	const char *slot = PS(x->c, w->slot);
	static char n1[12], n2[12];
	int a, vi;

	switch (act) {
	case A_PAGE:
		a = page_target(x, arg);
		if (slot[0] && slot[0] != '*')
			scopy(x->dslot, slot, sizeof(x->dslot));
		cfg_go(x, a);
		break;
	case A_HA:
		ha_do(x, slot, arg);
		break;
	case A_BACKLIGHT:
		vi = var_find(x, "backlight");
		v = clampi(have_v ? v : var_get(x, "backlight"), BL_MIN, 100);
		set_var(x, vi, v);
		gui_utoa(n1, (u32)v * 255 / 100);
		{
			const char *argv[] = { "/bin/homtom-bl", n1, 0 };

			spawn(x, SLOT_BACKLIGHT, argv, "backlight");
		}
		break;
	case A_VOLUME:
		set_var(x, var_find(x, "volume"), clampi(have_v ? v : var_get(x, "volume"), 0, 100));
		break;
	case A_MUTE:
		set_var(x, var_find(x, "mute"), have_v ? v != 0 : !var_get(x, "mute"));
		break;
	case A_BEEP:
		vi = var_get(x, "volume");
		if (var_get(x, "mute") || vi <= 0) {
			cfg_set_note(x, "muted", 0);
			break;
		}
		gui_utoa(n1, (u32)clampi(have_v ? v : 1000, 200, 4000));
		gui_utoa(n2, (u32)(VOL_RAW_MIN + clampi(vi, 0, 100) * (VOL_RAW_MAX - VOL_RAW_MIN) / 100));
		{
			const char *argv[] = { "/bin/homtom-snd2", "--hz", n1, "--secs", "1", "--vol", n2, 0 };

			spawn(x, SLOT_AUDIO, argv, "beep");
		}
		cfg_emit(x, "beep", "beep", vi);
		break;
	case A_CMD:
		a = cmd_find(arg);
		vi = w->type == W_TOGGLE && w->var >= 0 ? w->var : -1;
		if (a < 0 && vi >= 0)
			a = cmd_pair(arg, x->var[vi].value != 0);
		if (a < 0)
			break;
		if (spawn(x, cmd_slot[a], cmd_argv[a], cmd_name[a]) < 0) {
			if (vi >= 0)
				x->var[vi].value = !x->var[vi].value;	/* refused: the switch keeps showing the real state */
			break;
		}
		cfg_emit(x, "cmd", cmd_name[a], 0);
		break;
	case A_EVENT:
		cfg_emit(x, "press", arg, have_v ? v : w->var >= 0 ? x->var[w->var].value : 1);
		break;
	case A_STORAGE:
		cfg_emit(x, "reboot", "storage", 1);
		{
			const char *argv[] = { "/bin/reboot", 0 };

			spawn(x, SLOT_REBOOT, argv, "reboot");
		}
		cfg_set_note(x, "rebooting to storage mode", 0);
		break;
	case A_RELOAD:
		x->reload = 1;
		break;
	case A_SCREEN:
		cfg_lock(x, arg[0] == 'l' ? 1 : arg[0] == 'u' ? 0 : !x->locked);
		break;
	case A_TZ:
		tz_action(x, arg);
		break;
	case A_SET:
		if (have_v)
			set_var(x, w->var, v);
		break;
	default:
		break;
	}
}

static void run(cfg_ctx *x, int idx, int v, int have_v, int confirmed)
{
	cfg_widget *w = &x->c->w[idx];
	const char *arg = PS(x->c, w->arg);

	if ((w->flags & WF_CONFIRM) && !confirmed) {
		x->confirm = idx;
		return;
	}
	if (!have_v && (w->flags & WF_ARG))
		have_v = pnum(arg, &v);
	act(x, idx, w->action, arg, v, have_v);
}

/* backlight for a hold hint on a locked screen: the saved level (on), or off again; the lock flag is not touched */
static int hint_light(cfg_ctx *x, int on)
{
	char n[12];
	const char *argv[] = { "/bin/homtom-bl", n, 0 };
	int v = var_get(x, "backlight");

	gui_utoa(n, on ? (u32)clampi(v < BL_MIN ? 100 : v, BL_MIN, 100) * 255 / 100 : 0);
	return spawn(x, SLOT_BACKLIGHT, argv, "backlight");
}

static void hint_end(cfg_ctx *x)
{
	if (x->ovl_lit && x->locked)
		hint_light(x, 0);
	x->ovl = x->ovl_lit = 0;
}

int cfg_key(cfg_ctx *x, const char *name)
{
	int i, hit = 0;

	if (seq(name, "hold5") || seq(name, "hold10")) {
		/* the key daemon's hold hints: show them even on a locked screen, lighting the backlight for it */
		cfg_emit(x, "key", name, 1);
		x->last_act = x->now;
		if (x->locked && !x->ovl_lit)
			x->ovl_lit = hint_light(x, 1) == 0;
		x->ovl = name[4] == '5' ? 5 : 10;
		x->ovl_t = x->now;
		x->press = x->drag = x->hcand = -1;
		return 1;
	}
	if (!seq(name, "short") && !seq(name, "long"))
		return 0;
	cfg_emit(x, "key", name, 1);
	x->last_act = x->now;
	if (x->ovl) {
		/* the release that ends a hold: no lock, no page change */
		hint_end(x);
		return 1;
	}
	if (x->locked) {
		/* locked: a binding that is a screen action handles it; any other key unlocks first, then runs its binding */
		for (i = 0; i < x->c->nwidgets; i++)
			if (x->c->w[i].type == W_ON && seq(PS(x->c, x->c->w[i].key), name) && x->c->w[i].action == A_SCREEN)
				hit = 1;
		if (!hit)
			cfg_lock(x, 0);
		hit = 0;
	}
	for (i = 0; i < x->c->nwidgets; i++)
		if (x->c->w[i].type == W_ON && seq(PS(x->c, x->c->w[i].key), name)) {
			run(x, i, 0, 0, 0);
			hit = 1;
		}
	return hit || !x->locked;
}

/* ---------- touch ---------- */

static int hit_at_type(const cfg_ctx *x, int px, int py, int type)
{
	int i;

	for (i = 0; i < x->c->nwidgets; i++) {
		const cfg_widget *w = &x->c->w[i];

		if (w->page == x->page && w->type == type && gui_hit(w->x, w->y, w->w, w->h, px, py))
			return 1;
	}
	return 0;
}

/* a slider, or a draggable part of a lightctl (brightness, colour temperature, hue strip), under the finger */
static int drag_at(const cfg_ctx *x, int px, int py)
{
	int i, p;

	for (i = 0; i < x->c->nwidgets; i++) {
		const cfg_widget *w = &x->c->w[i];

		if (w->page != x->page)
			continue;
		if (w->type == W_SLIDER && gui_hit(w->x, w->y + w->h - 22 - imin(40, w->h - 48), w->w, imin(40, w->h - 48) + 20,
						   px, py))
			return i;
		if (w->type == W_LIGHTCTL && gui_hit(w->x, w->y, w->w, w->h, px, py) &&
		    ((p = lc_part(x, w, px, py)) == 3 || p == 4 || p == 17))
			return i | p << 8;
	}
	return -1;
}

/* a gesture that starts on a slider, a thermo or a lightctl slider/hue strip belongs to that widget and never swipes
   (the rest of a lightctl, e.g. its header row, starts swipes, so a full-page light control can still be swiped) */
static int control_at(const cfg_ctx *x, int px, int py)
{
	return drag_at(x, px, py) >= 0 || hit_at_type(x, px, py, W_SLIDER) || hit_at_type(x, px, py, W_THERMO);
}

static int hit_at(const cfg_ctx *x, int px, int py)
{
	const cfg_conf *c = x->c;
	int i, p, bx, bw;

	if (x->confirm >= 0) {
		if (gui_hit(CF_X + 16, CF_Y + CF_H - 60, 146, 48, px, py))
			return P_CANCEL;
		if (gui_hit(CF_X + CF_W - 162, CF_Y + CF_H - 60, 146, 48, px, py))
			return P_OK;
		return -1;
	}
	if (py < Y0) {
		if (has_back(x) && px < 48)
			return P_BACK;
		if (has_gear(x) && px >= 432)
			return P_GEAR;
		for (i = 0; i < c->npages; i++)
			if (nav_geom(x, i, &bx, &bw) && px >= bx && px < bx + bw)
				return P_NAV + i;
		return -1;
	}
	if (py >= 250 && (bw = page_dots(x))) {
		/* the 40 px either side of the dots step through the pages */
		bx = dots_x(x, &bw);
		if (px >= bx - 46 && px < bx - 6)
			return P_PREV;
		if (px > bx + (bw - 1) * 12 + 6 && px <= bx + (bw - 1) * 12 + 46)
			return P_NEXT;
	}
	for (i = 0; i < c->nwidgets; i++) {
		const cfg_widget *w = &c->w[i];

		if (w->page != x->page || w->type == W_SLIDER || (w->type == W_ROW && !w->action) || w->type == W_ON ||
		    !gui_hit(w->x, w->y, w->w, w->h, px, py))
			continue;
		if (w->type >= W_LIGHTCTL) {
			p = w->type == W_THERMO ? th_part(w, px, py) : lc_part(x, w, px, py);
			if (p)
				return i | p << 8;
		} else if (w->action || w->hold || w->type == W_TOGGLE) {
			return i;
		}
	}
	return -1;
}

static int slider_value(const cfg_widget *w, int px)
{
	int pct = gui_slider_pct(w->x + 12, w->w - 24, 0, px);
	int v = w->min + pct * (w->max - w->min) / 100;

	if (w->step > 1)
		v = w->min + (int)gui_udiv((u32)(v - w->min + w->step / 2), (u32)w->step) * w->step;
	return clampi(v, w->min, w->max);
}

/* finger on a lightctl drag part: the value under it, sent throttled when it changed */
static void lc_drag(cfg_ctx *x, const cfg_widget *w, int p, int px)
{
	const char *s = PS(x->c, w->slot);
	char old[24], *d = x->dop;
	int v, sat;

	scopy(old, d, sizeof(old));
	if (p == 17) {
		int sw = w->w - LC_TX - 100;

		v = (int)gui_udiv((u32)clampi(px - w->x - LC_TX, 0, sw - 1) * 360, (u32)sw);
		if (!hnum(x, s, "sat", &sat) || sat < 20)
			sat = 100;
		d = gui_utoa(gui_cat(d, "set_hue_sat:"), (u32)v);
		*d++ = ',';
		gui_utoa(d, (u32)clampi(sat, 0, 100));
	} else {
		int L = LC_FULL(w) ? LC_TX : lc_split(w);

		v = gui_slider_pct(w->x + L, w->w - L - (LC_FULL(w) ? 0 : 12), 0, px);
		if (p == 4)
			v = (ct_lo(w) + v * (ct_hi(w) - ct_lo(w)) / 100 + 25) / 50 * 50;
		gui_utoa(gui_cat(d, p == 3 ? "set_bri:" : "set_ct:"), (u32)v);
	}
	if (!seq(old, x->dop))
		ha_queue(x, s, x->dop, 0);
}

/* tap on a part of a lightctl or thermo */
static void part_tap(cfg_ctx *x, int idx, int p)
{
	const cfg_widget *w = &x->c->w[idx];
	const char *s = PS(x->c, w->slot), *v;
	char b[40], *d = b;
	int t;

	if (w->type == W_LIGHTCTL) {
		if (p == 18 && w->detail) {
			act(x, idx, A_PAGE, PS(x->c, w->detail), 0, 0);
		} else if (p == 19) {
			x->lcmore = !x->lcmore;
		} else if (p == 2 || p == 18) {
			ha_do(x, s, "toggle");
		} else {
			d = gui_utoa(gui_cat(b, "set_hue_sat:"), sw_hue[p - 5]);
			*d++ = ',';
			gui_utoa(d, sw_sat[p - 5]);
			ha_queue(x, s, b, 0);
		}
		return;
	}
	if (p == 3) {
		/* mode: the entry after the current one in modes=, the first when the current one is not listed */
		const char *l = PS(x->c, w->modes), *m = hf(x, s, "mode"), *q = l, *n = l;

		while (*q) {
			const char *e = q;

			while (*e && *e != ',')
				e++;
			if (m && (int)(e - q) == slen(m) && prefix(q, m)) {
				n = *e ? e + 1 : l;
				break;
			}
			q = *e ? e + 1 : e;
		}
		d = gui_cat(b, "mode:");
		while (*n && *n != ',')
			*d++ = *n++;
		*d = 0;
		ha_do(x, s, b);
		return;
	}
	v = hf(x, s, "target");
	if (!v || !tenths(v, &t))
		return;
	t = clampi(t + (p == 2 ? w->step : -w->step), w->min * 10, w->max * 10);
	gui_ftoa(gui_cat(b, "set_target:"), t);
	ha_queue(x, s, b, 1);
}

static void activate(cfg_ctx *x, int h)
{
	cfg_widget *w;

	if (h == P_CANCEL) {
		x->confirm = -1;
		return;
	}
	if (h == P_OK) {
		int i = x->confirm;

		x->confirm = -1;
		run(x, i, 0, 0, 1);
		return;
	}
	if (h == P_BACK) {
		cfg_go(x, page_target(x, "back"));
		return;
	}
	if (h == P_GEAR) {
		cfg_go(x, x->c->hub);
		return;
	}
	if (h == P_PREV || h == P_NEXT) {
		int a = page_walk(x, h == P_NEXT ? 1 : -1, x->c->wrap);

		if (a >= 0)
			cfg_go(x, a);
		return;
	}
	if (h >= P_NAV) {
		cfg_go(x, h - P_NAV);
		return;
	}
	if (h >> 8) {
		part_tap(x, h & 255, h >> 8);
		return;
	}
	w = &x->c->w[h];
	if (w->type == W_TOGGLE || (w->type == W_BUTTON && w->action == A_MUTE && !(w->flags & WF_ARG))) {
		int v = w->var >= 0 ? !x->var[w->var].value : 1;

		if (!w->action) {
			set_var(x, w->var, v);
			return;
		}
		if (w->var >= 0 && w->action != A_MUTE)
			x->var[w->var].value = v;
		run(x, h, v, 1, 0);
		return;
	}
	if (w->action)
		run(x, h, 0, 0, 0);
}

int cfg_touch(cfg_ctx *x, int px, int py, int down)
{
	int hit, redraw = 0, first, dx, dy;

	x->last_act = x->now;
	if (x->locked || x->ovl) {
		/* locked, or a hold hint is shown: every touch is ignored */
		x->down = down;
		return 0;
	}
	first = down && !x->down;
	x->down = down;
	if (first) {
		x->sx = px;
		x->sy = py;
		x->noswipe = control_at(x, px, py);
	}
	if (down) {
		x->lx = px;
		x->ly = py;
	}
	if (x->swallow) {
		/* a long press fired: nothing more until the finger lifts */
		if (!down)
			x->swallow = 0;
		return 0;
	}
	if (down && x->confirm < 0 && (x->drag >= 0 || (first && (hit = drag_at(x, px, py)) >= 0))) {
		cfg_widget *w;
		int v;

		if (x->drag < 0) {
			x->drag = hit;
			x->dop[0] = 0;
		}
		w = &x->c->w[x->drag & 255];
		if (x->drag >> 8) {
			lc_drag(x, w, x->drag >> 8, px);
			return 1;
		}
		v = slider_value(w, px);
		if (w->var >= 0 && v != x->var[w->var].value) {
			x->var[w->var].value = v;
			redraw = 1;
		}
		return redraw;
	}
	if (x->drag >= 0) {
		/* lightctl drags already sent (or queued) their last value */
		cfg_widget *w = &x->c->w[x->drag & 255];
		int v = w->var >= 0 ? x->var[w->var].value : 0;

		if (!(x->drag >> 8)) {
			if (w->action)
				run(x, (int)(w - x->c->w), v, 1, 0);
			else
				set_var(x, w->var, v);
		}
		x->drag = -1;
		return 1;
	}
	dx = x->lx - x->sx;
	dy = x->ly - x->sy;
	dy = dy < 0 ? -dy : dy;
	if (!down && !x->noswipe && x->confirm < 0 && (dx >= CFG_SWIPE_PX || -dx >= CFG_SWIPE_PX) && (dx < 0 ? -dx : dx) > 2 * dy) {
		/* swipe: left = next page, right = previous; the widget under the finger is not tapped */
		x->press = x->hcand = -1;
		hit = page_walk(x, dx < 0 ? 1 : -1, x->c->wrap);
		if (hit >= 0)
			cfg_go(x, hit);
		return 1;
	}
	hit = down ? hit_at(x, px, py) : -1;
	if (down) {
		if (hit >= 0 && hit != x->press) {
			x->t0 = x->now;
			x->px0 = px;
			x->py0 = py;
			x->hcand = hit < P_NAV && !(hit >> 8) && x->c->w[hit].hold ? hit : -1;
		} else if ((px - x->px0) * (px - x->px0) + (py - x->py0) * (py - x->py0) > CFG_HOLD_PX * CFG_HOLD_PX) {
			x->hcand = -1;
		}
		if (hit != x->press) {
			x->press = hit;
			redraw = 1;
		}
	} else if (x->press >= 0) {
		hit = x->press;
		x->press = x->hcand = -1;
		activate(x, hit);
		redraw = 1;
	}
	return redraw;
}

int cfg_tick(cfg_ctx *x)
{
	int i, r = 0;

	if (!x->last_act)
		x->last_act = x->now;
	if (x->c->autolock && !x->locked && !x->ovl && (int)(x->now - x->last_act) >= x->c->autolock * 1000)
		cfg_lock(x, 1);
	if (x->ovl && (int)(x->now - x->ovl_t) >= CFG_HINT_MS) {
		hint_end(x);
		r = 1;
	}
	if (x->hcand >= 0 && x->press == x->hcand && (int)(x->now - x->t0) >= CFG_HOLD_MS) {
		const cfg_widget *w = &x->c->w[x->hcand];

		i = x->hcand;
		x->press = x->hcand = -1;
		x->swallow = 1;
		act(x, i, w->hold, PS(x->c, w->harg), 0, 0);
		r = 1;
	}
	if (x->pend && (int)(x->now - x->due) >= 0) {
		x->pend = 0;
		ha_do(x, x->pslot, x->pop);
		r = 1;
	}
	for (i = 0; i < CFG_HA_OPT; i++)
		if (x->opt[i].k[0] && (int)(x->now - x->opt[i].until) >= 0) {
			x->opt[i].k[0] = 0;	/* not confirmed in time: show the file's value again */
			r = 1;
		}
	return r;
}

int cfg_wait(const cfg_ctx *x)
{
	int t = 1000, d;

	if (x->hcand >= 0 && (d = CFG_HOLD_MS - (int)(x->now - x->t0)) < t)
		t = d;
	if (x->pend && (d = (int)(x->due - x->now)) < t)
		t = d;
	return t < 10 ? 10 : t;
}
