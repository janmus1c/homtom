#include "ui.h"

#define LIGHT_MAX 1023	/* light_raw is a 10-bit ADC reading */

void ui_init(ui_state *s)
{
	char *p = (char *)s;
	u32 i;

	for (i = 0; i < sizeof(*s); i++)
		p[i] = 0;
	s->batt = s->light = -1;
	s->hh = s->mm = s->ss = -1;
	s->press = -1;
	s->slider = 60;
}

static int keq(const char *a, int alen, const char *b)
{
	int i;

	for (i = 0; i < alen; i++)
		if (!b[i] || a[i] != b[i])
			return 0;
	return !b[alen];
}

static int is_digit(char c) { return c >= '0' && c <= '9'; }

/* "12.34" -> 123 (tenths, truncated); "" or junk -> def */
static int tenths(const char *s, int def)
{
	int neg = 0, v = 0;

	if (*s == '-') {
		neg = 1;
		s++;
	}
	if (!is_digit(*s))
		return def;
	while (is_digit(*s))
		v = v * 10 + (*s++ - '0');
	v *= 10;
	if (*s == '.' && is_digit(s[1]))
		v += s[1] - '0';
	return neg ? -v : v;
}

static int whole(const char *s, int def)
{
	int t = tenths(s, 0x7fffffff);

	if (t == 0x7fffffff)
		return def;
	return t < 0 ? -((-t + 5) / 10) : (t + 5) / 10;
}

static int truthy(const char *s)
{
	return s[0] == '1' || s[0] == 't' || s[0] == 'T' || s[0] == 'y' || (s[0] == 'o' && s[1] == 'n');
}

static int split(char *s, char **f, int max)
{
	int n = 0;

	if (*s == '$')
		s++;
	f[n++] = s;
	for (; *s && n < max; s++) {
		if (*s == ',') {
			*s = 0;
			f[n++] = s + 1;
		} else if (*s == '*') {
			*s = 0;
			break;
		}
	}
	return n;
}

static void utc(ui_state *s, const char *t)
{
	int i;

	for (i = 0; i < 6; i++)
		if (!is_digit(t[i]))
			return;
	s->hh = (t[0] - '0') * 10 + t[1] - '0';
	s->mm = (t[2] - '0') * 10 + t[3] - '0';
	s->ss = (t[4] - '0') * 10 + t[5] - '0';
}

static void parse_gga(ui_state *s, char *v)
{
	char *f[16];
	int n = split(v, f, 16);

	utc(s, n > 1 ? f[1] : "");
	s->fixq = n > 6 ? whole(f[6], 0) : 0;
	s->sats = n > 7 ? whole(f[7], 0) : 0;
	s->hdop10 = n > 8 ? tenths(f[8], -1) : -1;
	s->alt10 = n > 9 ? tenths(f[9], 0) : 0;
}

static void parse_rmc(ui_state *s, char *v)
{
	char *f[16];
	int n = split(v, f, 16);

	if (s->hh < 0)
		utc(s, n > 1 ? f[1] : "");
	s->rmc_ok = n > 2 && f[2][0] == 'A';
	s->spd10 = n > 7 ? (int)((u32)tenths(f[7], 0) * 1852 / 1000) : 0;
	s->crs10 = n > 8 ? tenths(f[8], 0) : 0;
}

void ui_parse(ui_state *s, const char *buf, int len)
{
	int i = 0;

	s->have = 1;
	s->batt = s->light = -1;
	s->hh = s->mm = s->ss = -1;
	s->fixq = s->sats = s->rmc_ok = s->spd10 = s->crs10 = s->alt10 = 0;
	s->hdop10 = -1;
	while (i < len) {
		int ks = i, ke, vs, ve;
		char val[200];
		int n;

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
		while (i < len && (buf[i] == '\n' || buf[i] == '\r'))
			i++;
		if (ve - vs >= (int)sizeof(val))
			ve = vs + (int)sizeof(val) - 1;
		for (n = 0; n < ve - vs; n++)
			val[n] = buf[vs + n];
		val[n] = 0;
		n = ke - ks;
		if (keq(buf + ks, n, "battery_pct_est"))
			s->batt = whole(val, -1);
		else if (keq(buf + ks, n, "charging"))
			s->chg = truthy(val);
		else if (keq(buf + ks, n, "external_power"))
			s->ext = truthy(val);
		else if (keq(buf + ks, n, "light_raw"))
			s->light = whole(val, -1);
		else if (keq(buf + ks, n, "uptime_s"))
			s->up = (u32)whole(val, 0);
		else if (keq(buf + ks, n, "gga"))
			parse_gga(s, val);
		else if (keq(buf + ks, n, "rmc"))
			parse_rmc(s, val);
	}
}

/* drawing */

static void uptime_str(char *d, u32 up)
{
	u32 days = up / 86400, hrs = up / 3600 % 24, mins = up / 60 % 60;

	d = gui_cat(d, "up ");
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

static void label(int x, int y, const char *t)
{
	gui_text_tr(GUI_FONT_S, x, y, t, C_MUTED, 1);
}

static const char *fix_name(const ui_state *s)
{
	return s->fixq <= 0 ? "No fix" : s->fixq == 2 ? "DGPS fix" : "GPS fix";
}

static u16 fix_col(const ui_state *s)
{
	return s->fixq > 0 ? C_OK : C_WARN;
}

static void tile(int x, int y, int w, int h, const char *name, const char *val, u16 col)
{
	gui_card(x, y, w, h);
	label(x + 14, y + 8, name);
	gui_text(GUI_FONT_M, x + 14, y + 32, val, col);
}

static void page_home(const ui_state *s)
{
	char b[48], *p;
	int pct = s->light < 0 ? 0 : (int)((u32)(s->light > LIGHT_MAX ? LIGHT_MAX : s->light) * 100 / LIGHT_MAX);

	gui_card(8, 48, 228, 160);
	label(22, 56, "BATTERY");
	if (s->batt >= 0) {
		p = gui_utoa(b, (u32)s->batt);
		gui_cat(p, "%");
		gui_text(GUI_FONT_L, 22, 60, b, s->batt > 15 ? C_BONE : C_BAD);
	} else {
		gui_text(GUI_FONT_L, 22, 60, "--", C_MUTED);
	}
	gui_battery(22, 140, 200, 32, s->batt, s->chg);
	gui_text(GUI_FONT_S, 22, 181, s->chg ? "Charging" : s->ext ? "External power" : "On battery",
		 s->chg ? C_OK : C_MUTED);

	gui_card(244, 48, 228, 76);
	label(258, 56, "AMBIENT LIGHT");
	if (s->light >= 0) {
		p = gui_utoa(b, (u32)pct);
		gui_cat(p, " %");
		gui_text(GUI_FONT_M, 258, 72, b, C_BONE);
		p = gui_cat(b, "raw ");
		gui_utoa(p, (u32)s->light);
		gui_text_r(GUI_FONT_S, 258, 80, 200, b, C_MUTED);
	} else {
		gui_text(GUI_FONT_M, 258, 72, "--", C_MUTED);
	}
	gui_progress(258, 106, 200, 10, pct, C_BLUE);

	gui_card(244, 132, 228, 76);
	label(258, 140, "GPS");
	gui_dot(264, 175, 5, fix_col(s));
	gui_text(GUI_FONT_M, 276, 162, fix_name(s), C_BONE);
	p = gui_utoa(b, (u32)s->sats);
	gui_cat(p, " sats");
	gui_text_r(GUI_FONT_S, 258, 170, 200, b, C_MUTED);
	p = gui_ftoa(b, s->spd10);
	gui_cat(p, " km/h");
	gui_text(GUI_FONT_S, 258, 184, b, C_MUTED);
}

static void page_gps(const ui_state *s)
{
	char b[40], *p;

	tile(8, 48, 150, 76, "FIX", fix_name(s), fix_col(s));
	p = gui_utoa(b, (u32)s->sats);
	tile(165, 48, 150, 76, "SATELLITES", b, C_BONE);
	if (s->hh >= 0) {
		p = gui_pad2(b, (u32)s->hh);
		*p++ = ':';
		p = gui_pad2(p, (u32)s->mm);
		*p++ = ':';
		gui_pad2(p, (u32)s->ss);
	} else {
		gui_cat(b, "--");
	}
	tile(322, 48, 150, 76, "UTC", b, C_BONE);
	if (s->fixq > 0) {
		p = gui_ftoa(b, s->alt10);
		gui_cat(p, " m");
	} else {
		gui_cat(b, "--");
	}
	tile(8, 132, 150, 76, "ALTITUDE", b, C_BONE);
	p = gui_ftoa(b, s->rmc_ok ? s->spd10 : 0);
	gui_cat(p, " km/h");
	tile(165, 132, 150, 76, "SPEED", b, C_BONE);
	if (s->hdop10 >= 0)
		gui_ftoa(b, s->hdop10);
	else
		gui_cat(b, "--");
	tile(322, 132, 150, 76, "HDOP", b, s->hdop10 >= 0 && s->hdop10 <= 20 ? C_OK : C_BONE);
}

static void page_sys(const ui_state *s)
{
	char b[40], *p;

	tile(8, 48, 150, 76, "EXTERNAL POWER", s->ext ? "Yes" : "No", s->ext ? C_OK : C_BONE);
	tile(165, 48, 150, 76, "CHARGING", s->chg ? "Yes" : "No", s->chg ? C_OK : C_BONE);
	if (s->light >= 0)
		gui_utoa(b, (u32)s->light);
	else
		gui_cat(b, "--");
	tile(322, 48, 150, 76, "LIGHT RAW", b, C_BONE);

	gui_card(8, 132, 464, 76);
	label(22, 138, "SLIDER DEMO (NO EFFECT)");
	gui_slider(UI_SLIDER_X, UI_SLIDER_Y, UI_SLIDER_W, UI_SLIDER_H, s->slider, s->drag ? GUI_PRESSED : 0);
	p = gui_utoa(b, (u32)s->slider);
	gui_cat(p, " %");
	gui_text_r(GUI_FONT_M, 392, 162, 64, b, C_BONE);
}

void ui_draw(const ui_state *s)
{
	static const char *const names[UI_PAGES] = { "Home", "GPS", "System" };
	char b[40];
	int i;

	gui_unclip();
	gui_clear(C_INK);

	gui_mark(10, 6, 28, C_BLUE);
	gui_text_tr(GUI_FONT_M, 44, 6, "homtom", C_BONE, -1);
	gui_dot(462, 20, 5, s->have ? C_OK : C_BAD);
	uptime_str(b, s->up);
	if (s->have)
		gui_text_r(GUI_FONT_S, 300, 11, 144, b, C_MUTED);
	else
		gui_text_r(GUI_FONT_S, 300, 11, 144, "no data", C_BAD);
	gui_hline(0, 41, GUI_W, C_LINE);

	if (s->page == 1)
		page_gps(s);
	else if (s->page == 2)
		page_sys(s);
	else
		page_home(s);

	for (i = 0; i < UI_PAGES; i++)
		gui_button(8 + i * 157, UI_NAV_Y, 150, UI_NAV_H, names[i],
			   (s->page == i ? GUI_ACTIVE : 0) | (s->press == i ? GUI_PRESSED : 0));
}

static int nav_at(int x, int y)
{
	int i;

	for (i = 0; i < UI_PAGES; i++)
		if (gui_hit(8 + i * 157, UI_NAV_Y, 150, UI_NAV_H, x, y))
			return i;
	return -1;
}

int ui_touch(ui_state *s, int x, int y, int down)
{
	int hit = down ? nav_at(x, y) : -1, redraw = 0;

	if (down && s->page == 2 && (s->drag || gui_hit(UI_SLIDER_X - 12, UI_SLIDER_Y - 12, UI_SLIDER_W + 24,
							  UI_SLIDER_H + 24, x, y))) {
		int v = gui_slider_pct(UI_SLIDER_X, UI_SLIDER_W, UI_SLIDER_H, x);

		redraw = !s->drag || v != s->slider;
		s->drag = 1;
		s->slider = v;
		return redraw;
	}
	if (s->drag) {
		s->drag = 0;
		redraw = 1;
	}
	if (down) {
		if (hit != s->press) {
			s->press = hit;
			redraw = 1;
		}
	} else if (s->press >= 0) {
		s->page = s->press;
		s->press = -1;
		redraw = 1;
	}
	return redraw;
}
