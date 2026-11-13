/* statusd_core.h: portable core of homtom-statusd (NMEA parser, JSON and UI-file formatters).
 * No libc, no syscalls, no memcpy/memset: it builds freestanding for ARMv4T and with clang on the host for the test. */
#ifndef STATUSD_CORE_H
#define STATUSD_CORE_H

typedef unsigned int u32;
typedef unsigned char u8;

#define SD_NMEA_MAX 100
#define SD_EXTRA_MAX 1024	/* /tmp/ui.extra: bytes read */
#define SD_EXTRA_LINE 64	/* longest accepted key=value line, without the newline */
#define SD_EXTRA_KEYS 16	/* the GUI keeps 40 keys in all */

/* ADC filter (docs/ADC_ARBITRATION.md). Each 5 s cycle reads SD_ADC_N samples per channel; their median is checked
 * against an absolute window and against the median of the last SD_HIST accepted values. A rejected cycle keeps the
 * last good value; `confirm` consecutive rejected medians that agree with each other are a real step and are accepted;
 * SD_STALE_MAX rejected cycles in a row make the value unknown (-1) and clear the history. */
#define SD_ADC_N	5
#define SD_HIST		5
#define SD_CAND		3
#define SD_STALE_MAX	12	/* 60 s at one cycle per 5 s */
#define SD_PCT_HYST	2	/* battery_pct_est moves only by 2 points or more (or to 0/100) */
#define SD_CH_BAT	0
#define SD_CH_REF	1
#define SD_CH_LIGHT	2

struct sd_chcfg {
	int lo, hi;		/* plausible raw median */
	int jump;		/* largest accepted distance from the recent median */
	int confirm;		/* consecutive agreeing out-of-window medians that count as a real step */
};

/* battery mux 0 (656 on the live unit), reference mux 4 (about 380 with GPL14 high, 0 unpowered), light mux 2 (2-6) */
static const struct sd_chcfg sd_chcfg[3] = {
	{ 200, 1000, 40, 3 },
	{ 100, 1000, 30, 3 },
	{ 0, 1023, 150, 2 },
};

struct sd_ch {
	int hist[SD_HIST], nh;		/* accepted values, oldest first */
	int cand[SD_CAND], nc;		/* consecutive rejected but plausible medians */
	int stale;			/* consecutive cycles without an accepted value */
	u32 rejected;			/* cycles rejected since start */
};

struct sd_state {
	int bat, ref, light;		/* raw 10-bit ADC, -1 = unknown */
	int charging, ext_power, key;	/* 0/1, -1 = unknown */
	u32 uptime_s;
	int fix_valid;			/* a GGA with quality > 0 has been seen */
	u32 last_fix_up;		/* uptime_s of that GGA */
	int gga_seen, quality, sats;
	int speed_x100;			/* RMC speed over ground, hundredths of a knot, -1 = unknown */
	char gga[SD_NMEA_MAX + 1], rmc[SD_NMEA_MAX + 1];
	/* system fields, refreshed at request time; -1 = unknown */
	int load_x100[3];		/* /proc/loadavg 1, 5, 15 min, hundredths */
	int mem_free_kb, mem_total_kb;	/* MemAvailable (MemFree if absent), MemTotal */
	int bt_up, bt_links;		/* hci0 in /sys/class/bluetooth, number of hci0:<handle> ACL entries */
	int touch_x, touch_y, touch_type, touch_age;	/* /tmp/touch.last; touch_type 0 = none */
	char extra[SD_EXTRA_MAX + 1];	/* accepted /tmp/ui.extra lines, "key=value\n" each */
	int nextra;
	/* ADC filter and arbitration counters (docs/ADC_ARBITRATION.md) */
	struct sd_ch ch[3];
	int pct_hold;			/* battery_pct_est after hysteresis, -1 = none yet */
	u32 adc_cycles, adc_bad;	/* sample cycles; conversions lost to a timeout or a foreign ADCMUX change */
	u32 arb_ack, arb_none, arb_timeout, arb_busy;	/* touch acknowledged / no touch tool / 40 ms passed / request held */
	u32 arb_wait_ms, arb_wait_max;	/* last and longest wait for the acknowledgement */
};

#define SD_TOUCH_DOWN	1
#define SD_TOUCH_UP	2
#define SD_TOUCH_MOVE	3

static void sd_sys_unknown(struct sd_state *st)
{
	st->load_x100[0] = st->load_x100[1] = st->load_x100[2] = -1;
	st->mem_free_kb = st->mem_total_kb = -1;
	st->bt_up = st->bt_links = -1;
	st->touch_type = 0;
	st->touch_x = st->touch_y = st->touch_age = -1;
}

static void sd_init(struct sd_state *st)
{
	char *p = (char *)st;
	u32 i;

	for (i = 0; i < sizeof(*st); i++)
		p[i] = 0;
	st->bat = st->ref = st->light = -1;
	st->charging = st->ext_power = st->key = -1;
	st->speed_x100 = -1;
	st->pct_hold = -1;
	sd_sys_unknown(st);
}

/* ---- line assembly: feed bytes, a completed line is in lb->buf[0..n) without CR/LF ---- */
struct sd_lb {
	char buf[128];
	int n, over;
};

static int sd_lb_push(struct sd_lb *lb, char c)
{
	if (c == '\r')
		return 0;
	if (c == '\n') {
		int ok = lb->n > 0 && !lb->over;

		if (!ok)
			lb->n = 0;
		lb->over = 0;
		return ok;
	}
	if (c == '$') {			/* resync on a new sentence start */
		lb->n = 0;
		lb->over = 0;
	}
	if (lb->n >= (int)sizeof(lb->buf)) {
		lb->over = 1;
		return 0;
	}
	lb->buf[lb->n++] = c;
	return 0;
}

/* ---- NMEA ---- */
static int sd_hex(int c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/* "$...*HH" with a matching XOR checksum */
static int sd_nmea_ok(const char *s, int n)
{
	int i = 1, x = 0, h1, h2;

	if (n < 9 || s[0] != '$')
		return 0;
	while (i < n && s[i] != '*')
		x ^= (u8)s[i++];
	if (i + 3 != n)
		return 0;
	h1 = sd_hex(s[i + 1]);
	h2 = sd_hex(s[i + 2]);
	return h1 >= 0 && h2 >= 0 && ((h1 << 4) | h2) == x;
}

#define SD_MAXF 16

/* fields of the body (before '*'); returns the count */
static int sd_split(const char *s, int n, const char **f, int *fl)
{
	int i = 1, nf = 0, st = 1;

	while (i <= n && nf < SD_MAXF) {
		if (i == n || s[i] == ',' || s[i] == '*') {
			f[nf] = s + st;
			fl[nf] = i - st;
			nf++;
			st = i + 1;
			if (i == n || s[i] == '*')
				break;
		}
		i++;
	}
	return nf;
}

static int sd_uint(const char *s, int n)
{
	int v = 0, i;

	if (n <= 0 || n > 6)
		return -1;
	for (i = 0; i < n; i++) {
		if (s[i] < '0' || s[i] > '9')
			return -1;
		v = v * 10 + (s[i] - '0');
	}
	return v;
}

/* "13.4" -> 1340 (hundredths) */
static int sd_dec100(const char *s, int n)
{
	int ip = 0, i = 0, fr = 0, d = 0;

	if (n <= 0)
		return -1;
	while (i < n && s[i] >= '0' && s[i] <= '9' && i < 6)
		ip = ip * 10 + (s[i++] - '0');
	if (i == 0)
		return -1;
	if (i < n && s[i] == '.') {
		i++;
		while (i < n && s[i] >= '0' && s[i] <= '9') {
			if (d < 2) {
				fr = fr * 10 + (s[i] - '0');
				d++;
			}
			i++;
		}
	}
	if (i != n)
		return -1;
	while (d < 2) {
		fr *= 10;
		d++;
	}
	return ip * 100 + fr;
}

static void sd_store(char *dst, const char *s, int n)
{
	int i, o = 0;

	for (i = 0; i < n && o < SD_NMEA_MAX; i++)
		if (s[i] >= 0x20 && s[i] < 0x7f && s[i] != '"' && s[i] != '\\')
			dst[o++] = s[i];
	dst[o] = 0;
}

/* returns 1 for an accepted GGA, 2 for an accepted RMC, 0 otherwise (bad checksum, other sentence) */
static int sd_nmea_feed(struct sd_state *st, const char *s, int n, u32 up)
{
	const char *f[SD_MAXF];
	int fl[SD_MAXF], nf, q;

	if (n >= SD_NMEA_MAX || !sd_nmea_ok(s, n))
		return 0;
	if (s[1] < 'A' || s[1] > 'Z' || s[2] < 'A' || s[2] > 'Z')
		return 0;
	nf = sd_split(s, n, f, fl);
	if (s[3] == 'G' && s[4] == 'G' && s[5] == 'A') {
		if (nf < 8)
			return 0;
		q = sd_uint(f[6], fl[6]);
		st->quality = q < 0 ? 0 : q;
		q = sd_uint(f[7], fl[7]);
		st->sats = q < 0 ? 0 : q;
		st->gga_seen = 1;
		sd_store(st->gga, s, n);
		if (st->quality > 0) {
			st->fix_valid = 1;
			st->last_fix_up = up;
		}
		return 1;
	}
	if (s[3] == 'R' && s[4] == 'M' && s[5] == 'C') {
		if (nf < 8)
			return 0;
		st->speed_x100 = (fl[2] == 1 && f[2][0] == 'A') ? sd_dec100(f[7], fl[7]) : -1;
		sd_store(st->rmc, s, n);
		return 2;
	}
	return 0;
}

/* ---- system files (contents passed in by the device layer, one read each) ---- */

/* skip spaces, parse an unsigned decimal; -1 if no digit */
static int sd_num_at(const char *s, int n, int *i)
{
	int v = 0, d = 0;

	while (*i < n && s[*i] == ' ')
		(*i)++;
	while (*i < n && s[*i] >= '0' && s[*i] <= '9' && d < 9) {
		v = v * 10 + (s[(*i)++] - '0');
		d++;
	}
	return d ? v : -1;
}

/* "0.93 0.58 0.26 1/53 1541" -> 93, 58, 26 */
static void sd_parse_loadavg(struct sd_state *st, const char *s, int n)
{
	int i = 0, k;

	for (k = 0; k < 3; k++) {
		int ip, fr = 0, d = 0;

		st->load_x100[k] = -1;
		ip = sd_num_at(s, n, &i);
		if (ip < 0 || i >= n || s[i] != '.')
			return;
		i++;
		while (i < n && s[i] >= '0' && s[i] <= '9') {
			if (d < 2) {
				fr = fr * 10 + (s[i] - '0');
				d++;
			}
			i++;
		}
		if (d != 2)
			return;
		st->load_x100[k] = ip * 100 + fr;
	}
}

static int sd_prefix(const char *s, int n, int i, const char *p)
{
	for (; *p; p++, i++)
		if (i >= n || s[i] != *p)
			return 0;
	return 1;
}

/* MemTotal, MemFree and MemAvailable from the head of /proc/meminfo */
static void sd_parse_meminfo(struct sd_state *st, const char *s, int n)
{
	int i = 0, fr = -1, av = -1, tot = -1;

	while (i < n) {
		int j;

		if (sd_prefix(s, n, i, "MemTotal:")) {
			j = i + 9;
			tot = sd_num_at(s, n, &j);
		} else if (sd_prefix(s, n, i, "MemFree:")) {
			j = i + 8;
			fr = sd_num_at(s, n, &j);
		} else if (sd_prefix(s, n, i, "MemAvailable:")) {
			j = i + 13;
			av = sd_num_at(s, n, &j);
		}
		while (i < n && s[i] != '\n')
			i++;
		i++;
	}
	st->mem_total_kb = tot;
	st->mem_free_kb = av >= 0 ? av : fr;
}

/* one /sys/class/bluetooth entry name: "hci0" sets bt_up, "hci0:<digits>" is one ACL link. Start with bt_up = bt_links = 0. */
static void sd_bt_entry(struct sd_state *st, const char *name)
{
	int i;

	if (!sd_prefix(name, 5, 0, "hci0"))
		return;
	if (!name[4]) {
		st->bt_up = 1;
		return;
	}
	if (name[4] != ':' || !name[5])
		return;
	for (i = 5; name[i]; i++)
		if (name[i] < '0' || name[i] > '9')
			return;
	st->bt_links++;
}

/* /tmp/touch.last = "X Y down|up|move\n"; age from the file's mtime against the realtime clock. Garbage = none. */
static void sd_parse_touch(struct sd_state *st, const char *s, int n, u32 mtime, u32 rt_now)
{
	int i = 0, x, y, t = 0;

	st->touch_type = 0;
	st->touch_x = st->touch_y = st->touch_age = -1;
	x = sd_num_at(s, n, &i);
	y = sd_num_at(s, n, &i);
	while (i < n && s[i] == ' ')
		i++;
	if (sd_prefix(s, n, i, "down")) {
		t = SD_TOUCH_DOWN;
		i += 4;
	} else if (sd_prefix(s, n, i, "up")) {
		t = SD_TOUCH_UP;
		i += 2;
	} else if (sd_prefix(s, n, i, "move")) {
		t = SD_TOUCH_MOVE;
		i += 4;
	}
	if (x < 0 || y < 0 || !t || (i < n && s[i] != '\n' && s[i] != '\r' && s[i] != ' '))
		return;
	st->touch_x = x;
	st->touch_y = y;
	st->touch_type = t;
	st->touch_age = rt_now > mtime ? (int)(rt_now - mtime) : 0;
}

/* ---- /tmp/ui.extra: key=value lines from the tool-page wrappers, merged into the UI file and the JSON ---- */
static const char *const sd_own_keys[] = { "battery_pct_est", "charging", "external_power", "light_raw", "uptime_s", "gga",
					   "rmc", "gps_fix", "gps_sats", "gps_speed_kn", 0 };

static int sd_key_is(const char *k, int kn, const char *name)
{
	int i;

	for (i = 0; i < kn; i++)
		if (name[i] != k[i])
			return 0;
	return name[kn] == 0;
}

/* key already accepted into buf[0..o)? */
static int sd_extra_has(const char *buf, int o, const char *k, int kn)
{
	int i = 0, j;

	while (i < o) {
		for (j = 0; j < kn && i + j < o && buf[i + j] == k[j]; j++)
			;
		if (j == kn && i + j < o && buf[i + j] == '=')
			return 1;
		while (i < o && buf[i] != '\n')
			i++;
		i++;
	}
	return 0;
}

/* Accepts lines "key=value\n": key 1..31 of [a-z0-9_], not one of statusd's own UI keys and not repeated (first wins);
 * value printable ASCII without '"' and '\\'; at most SD_EXTRA_LINE characters and SD_EXTRA_KEYS lines. Only the first
 * SD_EXTRA_MAX bytes count and a last line without '\n' is ignored (a writer that has not finished it). */
static void sd_parse_extra(struct sd_state *st, const char *s, int n)
{
	int i = 0, o = 0, j;

	st->nextra = 0;
	if (n > SD_EXTRA_MAX)
		n = SD_EXTRA_MAX;
	while (i < n) {
		int b = i, e, eq = -1, ok = 1;

		while (i < n && s[i] != '\n')
			i++;
		if (i >= n)
			break;
		e = i++;
		if (e > b && s[e - 1] == '\r')
			e--;
		if (e - b > SD_EXTRA_LINE || st->nextra >= SD_EXTRA_KEYS)
			continue;
		for (j = b; j < e && eq < 0; j++)
			if (s[j] == '=')
				eq = j;
		if (eq <= b || eq - b > 31)
			continue;
		for (j = b; j < eq; j++)
			if (!((s[j] >= 'a' && s[j] <= 'z') || (s[j] >= '0' && s[j] <= '9') || s[j] == '_'))
				ok = 0;
		for (j = eq + 1; j < e; j++)
			if (s[j] < 0x20 || s[j] > 0x7e || s[j] == '"' || s[j] == '\\')
				ok = 0;
		for (j = 0; ok && sd_own_keys[j]; j++)
			if (sd_key_is(s + b, eq - b, sd_own_keys[j]))
				ok = 0;
		if (!ok || sd_extra_has(st->extra, o, s + b, eq - b))
			continue;
		for (j = b; j < e; j++)
			st->extra[o++] = s[j];
		st->extra[o++] = '\n';
		st->nextra++;
	}
	st->extra[o] = 0;
}

/* ---- battery estimate (same formula as userland/homtom-status; the 1:2 divider is unverified) ---- */
static int sd_bat_raw(const struct sd_state *st, u32 *mv, u32 *pct)
{
	if (st->bat < 0 || st->ref <= 0)
		return 0;
	*mv = (u32)st->bat * 2450u / (u32)st->ref;
	*pct = *mv < 3300 ? 0 : (*mv - 3300) / 9;	/* linear 3300..4200 mV */
	if (*pct > 100)
		*pct = 100;
	return 1;
}

/* the percentage after hysteresis once a sample cycle has set pct_hold */
static int sd_bat_est(const struct sd_state *st, u32 *mv, u32 *pct)
{
	if (!sd_bat_raw(st, mv, pct))
		return 0;
	if (st->pct_hold >= 0)
		*pct = (u32)st->pct_hold;
	return 1;
}

/* once per sample cycle */
static void sd_pct_update(struct sd_state *st)
{
	u32 mv, pct;
	int p;

	if (!sd_bat_raw(st, &mv, &pct)) {
		st->pct_hold = -1;
		return;
	}
	p = (int)pct;
	if (st->pct_hold < 0 || p >= st->pct_hold + SD_PCT_HYST || p <= st->pct_hold - SD_PCT_HYST || p == 0 || p == 100)
		st->pct_hold = p;
}

/* ---- ADC filter ---- */

/* median (lower middle for an even count) of the valid (>= 0) values; -1 with fewer than `need` */
static int sd_median(const int *s, int n, int need)
{
	int v[SD_ADC_N > SD_HIST ? SD_ADC_N : SD_HIST], k = 0, i, j;

	for (i = 0; i < n && k < (int)(sizeof(v) / sizeof(v[0])); i++) {
		int x = s[i];

		if (x < 0)
			continue;
		for (j = k; j > 0 && v[j - 1] > x; j--)
			v[j] = v[j - 1];
		v[j] = x;
		k++;
	}
	return k && k >= need ? v[(k - 1) / 2] : -1;
}

static int sd_dist(int a, int b)
{
	return a > b ? a - b : b - a;
}

static void sd_ch_accept(struct sd_ch *c, int v)
{
	int i;

	if (c->nh == SD_HIST) {
		for (i = 1; i < SD_HIST; i++)
			c->hist[i - 1] = c->hist[i];
		c->nh--;
	}
	c->hist[c->nh++] = v;
	c->nc = 0;
	c->stale = 0;
}

/* one cycle of n samples; returns the value to report (last accepted, or -1) */
static int sd_ch_feed(struct sd_ch *c, const struct sd_chcfg *k, const int *s, int n)
{
	int m = sd_median(s, n, 3), i;

	if (m >= k->lo && m <= k->hi) {
		if (!c->nh || sd_dist(m, sd_median(c->hist, c->nh, 1)) <= k->jump) {
			sd_ch_accept(c, m);
			return m;
		}
		if (c->nc && sd_dist(m, c->cand[c->nc - 1]) > k->jump)
			c->nc = 0;
		c->cand[c->nc++] = m;
		if (c->nc >= k->confirm || c->nc >= SD_CAND) {
			int nc = c->nc;

			c->nh = 0;
			for (i = 0; i < nc; i++)
				sd_ch_accept(c, c->cand[i]);
			return m;
		}
	} else {
		c->nc = 0;
	}
	c->rejected++;
	if (++c->stale >= SD_STALE_MAX) {
		c->stale = SD_STALE_MAX;
		c->nh = 0;
		return -1;
	}
	return c->nh ? c->hist[c->nh - 1] : -1;
}

/* s[channel][sample] for battery, reference, light; -1 marks a lost conversion */
static void sd_adc_cycle(struct sd_state *st, int s[3][SD_ADC_N])
{
	st->bat = sd_ch_feed(&st->ch[SD_CH_BAT], &sd_chcfg[SD_CH_BAT], s[SD_CH_BAT], SD_ADC_N);
	st->ref = sd_ch_feed(&st->ch[SD_CH_REF], &sd_chcfg[SD_CH_REF], s[SD_CH_REF], SD_ADC_N);
	st->light = sd_ch_feed(&st->ch[SD_CH_LIGHT], &sd_chcfg[SD_CH_LIGHT], s[SD_CH_LIGHT], SD_ADC_N);
	st->adc_cycles++;
	sd_pct_update(st);
}

/* milliseconds from a file's mtime to now on the same clock; no signed division (no libgcc on the device) */
static int sd_age_ms(u32 now_s, u32 now_ns, u32 mt_s, u32 mt_ns)
{
	int ds = (int)(now_s - mt_s);

	if (ds > 1000000)
		return 1000000000;
	if (ds < -1000000)
		return -1000000000;
	return ds * 1000 + (int)(now_ns / 1000000u) - (int)(mt_ns / 1000000u);
}

/* a request or acknowledgement file older than 2 s, or more than 2 s in the future (clock step), is left over */
#define SD_ARB_STALE_MS 2000
static int sd_arb_stale(int age_ms)
{
	return age_ms > SD_ARB_STALE_MS || age_ms < -SD_ARB_STALE_MS;
}

/* ---- output writer ---- */
struct sd_w {
	char *p, *e;
};

static void sd_ws(struct sd_w *w, const char *s)
{
	while (*s && w->p < w->e)
		*w->p++ = *s++;
}

static void sd_wu(struct sd_w *w, u32 v)
{
	char t[12];
	int n = 0;

	do {
		t[n++] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	while (n && w->p < w->e)
		*w->p++ = t[--n];
}

static void sd_wi(struct sd_w *w, int v)		/* negative = null */
{
	if (v < 0)
		sd_ws(w, "null");
	else
		sd_wu(w, (u32)v);
}

static int sd_wend(struct sd_w *w, char *buf)
{
	if (w->p >= w->e)
		w->p = w->e - 1;
	*w->p = 0;
	return (int)(w->p - buf);
}

/* JSON body: the fields and order of userland/homtom-status, then the system fields; unknown = null, no sentence = "" */
static int sd_json(const struct sd_state *st, char *buf, int cap)
{
	struct sd_w w;
	u32 mv = 0, pct = 0, age = 0;
	int est = sd_bat_est(st, &mv, &pct), i;

	w.p = buf;
	w.e = buf + cap;
	if (st->fix_valid)
		age = st->uptime_s > st->last_fix_up ? st->uptime_s - st->last_fix_up : 0;
	sd_ws(&w, "{\"uptime_s\":");
	sd_wu(&w, st->uptime_s);
	sd_ws(&w, ",\"battery_raw\":");
	sd_wi(&w, st->bat);
	sd_ws(&w, ",\"battery_ref_raw\":");
	sd_wi(&w, st->ref);
	sd_ws(&w, ",\"charging\":");
	sd_wi(&w, st->charging);
	sd_ws(&w, ",\"external_power\":");
	sd_wi(&w, st->ext_power);
	sd_ws(&w, ",\"power_key\":");
	sd_wi(&w, st->key);
	sd_ws(&w, ",\"battery_mv_est\":");
	if (est)
		sd_wu(&w, mv);
	else
		sd_ws(&w, "null");
	sd_ws(&w, ",\"battery_pct_est\":");
	if (est)
		sd_wu(&w, pct);
	else
		sd_ws(&w, "null");
	sd_ws(&w, ",\"light_raw\":");
	sd_wi(&w, st->light);
	sd_ws(&w, ",\"gps_age_s\":");
	if (st->fix_valid)
		sd_wu(&w, age);
	else
		sd_ws(&w, "null");
	sd_ws(&w, ",\"gga\":\"");
	sd_ws(&w, st->gga);
	sd_ws(&w, "\",\"rmc\":\"");
	sd_ws(&w, st->rmc);
	sd_ws(&w, "\",\"cpu_load\":");
	sd_wi(&w, st->load_x100[0]);
	sd_ws(&w, ",\"loadavg_x100\":");
	if (st->load_x100[0] < 0 || st->load_x100[1] < 0 || st->load_x100[2] < 0) {
		sd_ws(&w, "null");
	} else {
		sd_ws(&w, "[");
		sd_wu(&w, (u32)st->load_x100[0]);
		sd_ws(&w, ",");
		sd_wu(&w, (u32)st->load_x100[1]);
		sd_ws(&w, ",");
		sd_wu(&w, (u32)st->load_x100[2]);
		sd_ws(&w, "]");
	}
	sd_ws(&w, ",\"mem_free_kb\":");
	sd_wi(&w, st->mem_free_kb);
	sd_ws(&w, ",\"mem_total_kb\":");
	sd_wi(&w, st->mem_total_kb);
	sd_ws(&w, ",\"bt_up\":");
	sd_wi(&w, st->bt_up);
	sd_ws(&w, ",\"bt_links\":");
	sd_wi(&w, st->bt_links);
	sd_ws(&w, ",\"last_touch\":");
	if (!st->touch_type) {
		sd_ws(&w, "null");
	} else {
		sd_ws(&w, "{\"x\":");
		sd_wu(&w, (u32)st->touch_x);
		sd_ws(&w, ",\"y\":");
		sd_wu(&w, (u32)st->touch_y);
		sd_ws(&w, ",\"type\":\"");
		sd_ws(&w, st->touch_type == SD_TOUCH_DOWN ? "down" : st->touch_type == SD_TOUCH_UP ? "up" : "move");
		sd_ws(&w, "\",\"age_s\":");
		sd_wu(&w, (u32)st->touch_age);
		sd_ws(&w, "}");
	}
	sd_ws(&w, ",\"adc\":{\"cycles\":");
	sd_wu(&w, st->adc_cycles);
	sd_ws(&w, ",\"bad_conv\":");
	sd_wu(&w, st->adc_bad);
	for (i = 0; i < 6; i++) {
		sd_ws(&w, i == 0 ? ",\"rejected\":[" : i == 3 ? "],\"stale\":[" : ",");
		sd_wu(&w, i < 3 ? st->ch[i].rejected : (u32)st->ch[i - 3].stale);
	}
	sd_ws(&w, "],\"ack\":");
	sd_wu(&w, st->arb_ack);
	sd_ws(&w, ",\"no_touch\":");
	sd_wu(&w, st->arb_none);
	sd_ws(&w, ",\"timeout\":");
	sd_wu(&w, st->arb_timeout);
	sd_ws(&w, ",\"busy\":");
	sd_wu(&w, st->arb_busy);
	sd_ws(&w, ",\"wait_ms\":");
	sd_wu(&w, st->arb_wait_ms);
	sd_ws(&w, ",\"wait_max_ms\":");
	sd_wu(&w, st->arb_wait_max);
	sd_ws(&w, "}");
	sd_ws(&w, ",\"extra\":{");
	{
		const char *p = st->extra;

		while (*p) {
			sd_ws(&w, p == st->extra ? "\"" : ",\"");
			while (*p != '=' && w.p < w.e)
				*w.p++ = *p++;
			sd_ws(&w, "\":\"");
			for (p++; *p != '\n' && w.p < w.e; )
				*w.p++ = *p++;
			sd_ws(&w, "\"");
			while (*p && *p != '\n')
				p++;
			if (*p)
				p++;
		}
	}
	sd_ws(&w, "}}");
	return sd_wend(&w, buf);
}

/* /tmp/homtom-ui.txt key=value lines (workspace/gui/README.md); unknown values are left out */
static int sd_ui(const struct sd_state *st, char *buf, int cap)
{
	struct sd_w w;
	u32 mv, pct;

	w.p = buf;
	w.e = buf + cap;
	if (sd_bat_est(st, &mv, &pct)) {
		sd_ws(&w, "battery_pct_est=");
		sd_wu(&w, pct);
		sd_ws(&w, "\n");
	}
	if (st->charging >= 0) {
		sd_ws(&w, "charging=");
		sd_wu(&w, (u32)st->charging);
		sd_ws(&w, "\n");
	}
	if (st->ext_power >= 0) {
		sd_ws(&w, "external_power=");
		sd_wu(&w, (u32)st->ext_power);
		sd_ws(&w, "\n");
	}
	if (st->light >= 0) {
		sd_ws(&w, "light_raw=");
		sd_wu(&w, (u32)st->light);
		sd_ws(&w, "\n");
	}
	sd_ws(&w, "uptime_s=");
	sd_wu(&w, st->uptime_s);
	sd_ws(&w, "\n");
	if (st->gga[0]) {
		sd_ws(&w, "gga=");
		sd_ws(&w, st->gga);
		sd_ws(&w, "\n");
	}
	if (st->rmc[0]) {
		sd_ws(&w, "rmc=");
		sd_ws(&w, st->rmc);
		sd_ws(&w, "\n");
	}
	if (st->gga_seen) {
		sd_ws(&w, "gps_fix=");
		sd_wu(&w, (u32)st->quality);
		sd_ws(&w, "\ngps_sats=");
		sd_wu(&w, (u32)st->sats);
		sd_ws(&w, "\n");
	}
	if (st->speed_x100 >= 0) {
		u32 v = (u32)st->speed_x100 / 10;	/* tenths of a knot */

		sd_ws(&w, "gps_speed_kn=");
		sd_wu(&w, v / 10);
		sd_ws(&w, ".");
		sd_wu(&w, v % 10);
		sd_ws(&w, "\n");
	}
	sd_ws(&w, st->extra);
	return sd_wend(&w, buf);
}

#endif
