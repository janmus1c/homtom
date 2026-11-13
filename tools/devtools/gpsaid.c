/* gpsaid: give the GO 630 SiRF III (ttySAC2, NMEA 4800) an approximate position and GPS time, so it starts warm
 * instead of cold. Freestanding ARMv4T (raw EABI syscalls, no libc, no libgcc). Design and test plan: docs/GPS_AIDING.md.
 *
 *   gpsaid [--send] --lat DEG --lon DEG [--alt M] --time UNIX|now [options]
 *   gpsaid [--send] --to-nmea
 *   gpsaid --selftest
 *
 * Without --send nothing is opened: the frames are printed as text and hex (dry run). With --send the port is opened
 * write-only, its termios is read but never changed unless --set-4800 is given, and the only bytes written are the frames
 * printed in dry run. The tool never reads the port (homtom-statusd stays the one reader) and never touches GPIO.
 *
 * Modes:
 *   nmea104 (default)  one $PSRF104 (LLA navigation initialization) at the current 4800 baud; no protocol switch.
 *   nmea101            one $PSRF101 (ECEF navigation initialization); stock sirfreset sends this sentence (with zeros).
 *   sirf               $PSRF100 to SiRF binary at 4800, MID 128 (Initialize Data Source), then the stock MID 129 frame
 *                      back to NMEA at 4800, sent twice. Use only if the NMEA sentences are ignored.
 *   --to-nmea          only the stock MID 129 frame (recovery if the receiver was left in SiRF binary at 4800).
 *
 * Options: --port PATH (/dev/ttySAC2), --leap S (18), --week-offset N (0; -1024 tests the week rollover handling),
 * --drift HZ (0 = receiver keeps its last value), --channels N (12), --reset warm-init|hot-init (warm-init),
 * --set-4800 (TCSETS raw 4800 8N1 like homtom-statusd; only when no other process owns the port).
 * Exit: 0 ok, 2 usage, 3 refused (range or port speed), 4 port error, 5 selftest failure. */
typedef unsigned int u32;
typedef int s32;
typedef unsigned char u8;
typedef long long s64;

#define ONE_Q30 (1 << 30)
#define E2_Q30 7188036		/* WGS84 first eccentricity squared, Q30 */
#define OME2_Q30 1066553788	/* 1 - e^2, Q30 */
#define A_Q8 ((s64)6378137 << 8)	/* WGS84 semi-major axis, metres Q8 */
#define UDEG_TO_RAD_Q54 314410567	/* pi / 180e6 in Q30 radians per microdegree, scaled by 2^24 */
#define CORDIC_K_Q30 652032874
#define GPS_EPOCH_UNIX 315964800u
#define WEEK_S 604800u

static const s32 atan_q30[31] = {
	843314857, 497837829, 263043837, 133525159, 67021687, 33543516, 16775851, 8388437, 4194283, 2097149, 1048576,
	524288, 262144, 131072, 65536, 32768, 16384, 8192, 4096, 2048, 1024, 512, 256, 128, 64, 32, 16, 8, 4, 2, 1
};

/* Stock sirfreset's canned "Switch to NMEA" payload (rodata 0x17b33 of /bin/sirfreset, ttsystem.bak 2013-04-04):
 * mode 2, GGA 1 s, GLL off, GSA 1 s, GSV 5 s, RMC 1 s, VTG off, MSS off, ZDA off, 4800 baud. */
static const u8 mid129_stock[24] = {
	0x81, 0x02, 0x01, 0x01, 0x00, 0x01, 0x01, 0x01, 0x05, 0x01, 0x01, 0x01,
	0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x12, 0xc0
};

struct aid {
	s32 lat_udeg, lon_udeg, alt_m;
	s32 x, y, z;		/* ECEF metres */
	s32 drift_hz;
	u32 week, tow_s;
	u32 channels;
	int hot;		/* 0 warm start with init data, 1 hot start with init data */
};

/* ---- arithmetic without libgcc ---- */
static u32 udivmod(u32 n, u32 d, u32 *rem)
{
	u32 q = 0, r = 0;
	int i;

	for (i = 31; i >= 0; i--) {
		r = (r << 1) | ((n >> i) & 1);
		if (r >= d) {
			r -= d;
			q |= 1u << i;
		}
	}
	if (rem)
		*rem = r;
	return q;
}

static void cordic(s32 z, s32 *c, s32 *s)
{
	s32 x = CORDIC_K_Q30, y = 0, t;
	int i;

	for (i = 0; i < 31; i++) {
		if (z >= 0) {
			t = x - (y >> i);
			y = y + (x >> i);
			z -= atan_q30[i];
		} else {
			t = x + (y >> i);
			y = y - (x >> i);
			z += atan_q30[i];
		}
		x = t;
	}
	*c = x;
	*s = y;
}

static s32 udeg_to_q30(s32 udeg)
{
	return (s32)(((s64)udeg * UDEG_TO_RAD_Q54) >> 24);
}

static s32 round_q8(s64 v)
{
	return (s32)((v + 128) >> 8);
}

/* WGS84 geodetic (microdegrees, metres) to ECEF metres, Q30 fixed point; about 1 m from the double-precision result. */
static void lla_to_ecef(s32 lat_udeg, s32 lon_udeg, s32 alt_m, s32 *x, s32 *y, s32 *z)
{
	s32 cp, sp, cl, sl, flip = 0;
	s64 s2, e, e2, e3, k, n, r, h = (s64)alt_m << 8;

	if (lon_udeg > 90000000) {
		lon_udeg -= 180000000;
		flip = 1;
	} else if (lon_udeg < -90000000) {
		lon_udeg += 180000000;
		flip = 1;
	}
	cordic(udeg_to_q30(lat_udeg), &cp, &sp);
	cordic(udeg_to_q30(lon_udeg), &cl, &sl);
	if (flip) {
		cl = -cl;
		sl = -sl;
	}
	s2 = ((s64)sp * sp) >> 30;
	e = (E2_Q30 * s2) >> 30;
	e2 = (e * e) >> 30;
	e3 = (e2 * e) >> 30;
	k = ONE_Q30 + (e >> 1) + ((3 * e2) >> 3) + ((5 * e3) >> 4);	/* 1/sqrt(1 - e^2 sin^2 lat), error < 1e-9 */
	n = (A_Q8 * k) >> 30;
	r = ((n + h) * cp) >> 30;
	*x = round_q8((r * cl) >> 30);
	*y = round_q8((r * sl) >> 30);
	*z = round_q8(((((n * OME2_Q30) >> 30) + h) * sp) >> 30);
}

/* UTC seconds since 1970 to GPS week and time of week; leap = GPS-UTC offset in seconds. */
static int gps_time(u32 unix_s, s32 leap, s32 week_offset, u32 *week, u32 *tow)
{
	u32 g, w;

	if (unix_s < GPS_EPOCH_UNIX)
		return -1;
	g = unix_s - GPS_EPOCH_UNIX + (u32)leap;
	w = udivmod(g, WEEK_S, tow);
	*week = (u32)((s32)w + week_offset);
	return 0;
}

/* ---- framing ---- */
static u32 sirf_checksum(const u8 *p, u32 n)
{
	u32 s = 0, i;

	for (i = 0; i < n; i++)
		s += p[i];
	return s & 0x7fff;
}

static u32 sirf_frame(const u8 *p, u32 n, u8 *out)
{
	u32 c = sirf_checksum(p, n), i;

	out[0] = 0xa0;
	out[1] = 0xa2;
	out[2] = (u8)(n >> 8);
	out[3] = (u8)n;
	for (i = 0; i < n; i++)
		out[4 + i] = p[i];
	out[4 + n] = (u8)(c >> 8);
	out[5 + n] = (u8)c;
	out[6 + n] = 0xb0;
	out[7 + n] = 0xb3;
	return n + 8;
}

static void be32(u8 *p, u32 v)
{
	p[0] = (u8)(v >> 24);
	p[1] = (u8)(v >> 16);
	p[2] = (u8)(v >> 8);
	p[3] = (u8)v;
}

/* MID 128 Initialize Data Source, 25 bytes. Reset byte: bit 0 init data valid, bit 1 clear ephemeris (warm),
 * bit 7 as stock sirfreset always sets it. */
static void mid128(const struct aid *a, u8 *p)
{
	p[0] = 0x80;
	be32(p + 1, (u32)a->x);
	be32(p + 5, (u32)a->y);
	be32(p + 9, (u32)a->z);
	be32(p + 13, (u32)a->drift_hz);
	be32(p + 17, a->tow_s * 100);
	p[21] = (u8)(a->week >> 8);
	p[22] = (u8)a->week;
	p[23] = (u8)a->channels;
	p[24] = a->hot ? 0x81 : 0x83;
}

struct buf {
	char b[160];
	u32 n;
};

/* Explicit reset instead of an aggregate initializer: gcc would emit memcpy/memset, and there is no libc. */
static void binit(struct buf *o)
{
	o->n = 0;
	o->b[0] = 0;
}

static void bs(struct buf *o, const char *s)
{
	while (*s && o->n < sizeof(o->b) - 1)
		o->b[o->n++] = *s++;
	o->b[o->n] = 0;
}

static void bu(struct buf *o, u32 v)
{
	char t[12];
	int i = 0;
	u32 r;

	do {
		v = udivmod(v, 10, &r);
		t[i++] = (char)('0' + r);
	} while (v);
	while (i && o->n < sizeof(o->b) - 1)
		o->b[o->n++] = t[--i];
	o->b[o->n] = 0;
}

static void bi(struct buf *o, s32 v)
{
	if (v < 0) {
		bs(o, "-");
		bu(o, (u32)-v);
	} else
		bu(o, (u32)v);
}

static void bx(struct buf *o, u32 v, int digits)
{
	char t[2] = { 0, 0 };

	while (digits--) {
		t[0] = "0123456789ABCDEF"[(v >> (4 * digits)) & 15];
		bs(o, t);
	}
}

/* Microdegrees as decimal degrees with 6 places. */
static void bdeg(struct buf *o, s32 udeg)
{
	u32 a = (u32)(udeg < 0 ? -udeg : udeg), f, i;
	char t[7];

	if (udeg < 0)
		bs(o, "-");
	bu(o, udivmod(a, 1000000, &f));
	for (i = 0; i < 6; i++) {
		f = udivmod(f, 10, &a);
		t[5 - i] = (char)('0' + a);
	}
	t[6] = 0;
	bs(o, ".");
	bs(o, t);
}

/* Wrap body (no '$') as "$BODY*CS\r\n". */
static void nmea_finish(struct buf *o, const char *body)
{
	u32 c = 0;
	const char *s;

	for (s = body; *s; s++)
		c ^= (u8)*s;
	o->n = 0;
	bs(o, "$");
	bs(o, body);
	bs(o, "*");
	bx(o, c, 2);
	bs(o, "\r\n");
}

static void psrf104(const struct aid *a, struct buf *o)
{
	struct buf b;

	binit(&b);
	bs(&b, "PSRF104,");
	bdeg(&b, a->lat_udeg);
	bs(&b, ",");
	bdeg(&b, a->lon_udeg);
	bs(&b, ",");
	bi(&b, a->alt_m);
	bs(&b, ",");
	bi(&b, a->drift_hz);
	bs(&b, ",");
	bu(&b, a->tow_s);
	bs(&b, ",");
	bu(&b, a->week);
	bs(&b, ",");
	bu(&b, a->channels);
	bs(&b, a->hot ? ",1" : ",3");
	nmea_finish(o, b.b);
}

static void psrf101(const struct aid *a, struct buf *o)
{
	struct buf b;

	binit(&b);
	bs(&b, "PSRF101,");
	bi(&b, a->x);
	bs(&b, ",");
	bi(&b, a->y);
	bs(&b, ",");
	bi(&b, a->z);
	bs(&b, ",");
	bi(&b, a->drift_hz);
	bs(&b, ",");
	bu(&b, a->tow_s);
	bs(&b, ",");
	bu(&b, a->week);
	bs(&b, ",");
	bu(&b, a->channels);
	bs(&b, a->hot ? ",1" : ",3");
	nmea_finish(o, b.b);
}

static void psrf100_sirf4800(struct buf *o)
{
	nmea_finish(o, "PSRF100,0,4800,8,1,0");
}

/* ---- parsing ---- */
static int streq(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

/* Decimal number with up to `places` fractional digits (more are rounded away) to an integer scaled by 10^places. */
static int parse_fixed(const char *s, int places, s32 *out)
{
	u32 v = 0;
	int neg = 0, frac = -1, digits = 0, round_up = 0;

	if (*s == '-' || *s == '+')
		neg = *s++ == '-';
	for (; *s; s++) {
		if (*s == '.' && frac < 0) {
			frac = 0;
			continue;
		}
		if (*s < '0' || *s > '9')
			return -1;
		if (frac >= places) {
			if (frac == places && *s >= '5')
				round_up = 1;
			frac++;
			continue;
		}
		if (v > 200000000u)
			return -1;
		v = v * 10 + (u32)(*s - '0');
		digits++;
		if (frac >= 0)
			frac++;
	}
	if (!digits)
		return -1;
	if (frac < 0)
		frac = 0;
	for (; frac < places; frac++) {
		if (v > 400000000u)
			return -1;
		v *= 10;
	}
	v += (u32)round_up;
	if (v > 0x7fffffffu)
		return -1;
	*out = neg ? -(s32)v : (s32)v;
	return 0;
}

#ifndef GPSAID_NO_MAIN
/* ---- device layer ---- */
static long sc(long n, long a, long b, long c, long d, long e, long f)
{
	register long r7 __asm__("r7") = n;
	register long r0 __asm__("r0") = a;
	register long r1 __asm__("r1") = b;
	register long r2 __asm__("r2") = c;
	register long r3 __asm__("r3") = d;
	register long r4 __asm__("r4") = e;
	register long r5 __asm__("r5") = f;

	__asm__ volatile("svc 0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5) : "memory");
	return r0;
}

#define SYS_write 4
#define SYS_open 5
#define SYS_close 6
#define SYS_gettimeofday 78
#define SYS_setitimer 104
#define SYS_nanosleep 162
#define SYS_ioctl 54
#define TCGETS 0x5401
#define TCSETS 0x5402
#define CBAUD 0x100f
#define B4800 0xc
#define OPOST 0x1

static void put(const char *s)
{
	long n = 0;

	while (s[n])
		n++;
	sc(SYS_write, 1, (long)s, n, 0, 0, 0);
}

static void putb(struct buf *o)
{
	put(o->b);
}

static void msleep(u32 ms)
{
	u32 rem;
	long ts[2];

	ts[0] = (long)udivmod(ms, 1000, &rem);
	ts[1] = (long)(rem * 1000000);
	sc(SYS_nanosleep, (long)ts, 0, 0, 0, 0, 0);
}

static void hexdump(const char *tag, const u8 *p, u32 n)
{
	struct buf o;
	u32 i;

	put(tag);
	for (i = 0; i < n; i++) {
		binit(&o);
		bs(&o, " ");
		bx(&o, p[i], 2);
		putb(&o);
	}
	put("\n");
}

static long port = -1;

static int send(const u8 *p, u32 n)
{
	u32 off = 0;
	int tries = 0;

	if (port < 0)
		return 0;
	while (off < n) {
		long w = sc(SYS_write, port, (long)(p + off), n - off, 0, 0, 0);

		if (w > 0) {
			off += (u32)w;
			continue;
		}
		if (w != -11 || ++tries > 50)	/* EAGAIN: wait for the UART FIFO */
			return -1;
		msleep(20);
	}
	return 0;
}

static int emit_nmea(struct buf *o)
{
	put("nmea:");
	put(" ");
	put(o->b);
	hexdump("hex:", (const u8 *)o->b, o->n);
	return send((const u8 *)o->b, o->n);
}

static int emit_sirf(const char *tag, const u8 *pl, u32 n)
{
	u8 f[64];
	u32 len = sirf_frame(pl, n, f);

	put(tag);
	put("\n");
	hexdump("hex:", f, len);
	return send(f, len);
}

static void usage(void)
{
	put("usage: gpsaid [--send] --lat DEG --lon DEG [--alt M] --time UNIX|now [--mode nmea104|nmea101|sirf]\n"
	    "              [--port P] [--leap S] [--week-offset N] [--drift HZ] [--channels N] [--reset warm-init|hot-init]\n"
	    "              [--set-4800]\n"
	    "       gpsaid [--send] [--port P] [--set-4800] --to-nmea\n"
	    "       gpsaid --selftest\n");
}

static int selftest(void)
{
	/* Example payload of MID 128 in the SiRF Binary Protocol Reference Manual (as recalled; its checksum 0x0A91 matches). */
	static const u8 ex128[25] = {
		0x80, 0xff, 0xd7, 0x00, 0xf9, 0xff, 0xbe, 0x52, 0x66, 0x00, 0x3a, 0xc5, 0x7a,
		0x00, 0x01, 0x24, 0xf8, 0x00, 0x83, 0xd6, 0x00, 0x03, 0x9c, 0x0c, 0x33
	};
	/* Dummy points (public landmarks, not user data) and double-precision ECEF from Python. */
	static const s32 pts[][6] = {
		{ 51477800, 0, 46, 3980610, 0, 4966861 },
		{ 0, 0, 0, 6378137, 0, 0 },
		{ -33856800, 151215300, 10, -4646976, 2553081, -3533273 },
		{ 37387511, -121972320, 0, -2686729, -4304290, 3851652 },
		{ 89900000, 179900000, 1000, -11171, 19, 6357743 },
		{ -45500000, -170250000, -30, -4413425, -758365, -4526448 },
		{ 10000000, 95000000, 5000, -547930, 6262874, 1101117 },
	};
	struct buf o;
	u32 i, w, t, bad = 0;
	s32 x, y, z;

	if (sirf_checksum(ex128, 25) != 0x0a91)
		bad |= 1;
	nmea_finish(&o, "PSRF100,0,9600,8,1,0");
	if (!streq(o.b, "$PSRF100,0,9600,8,1,0*0C\r\n"))
		bad |= 2;
	for (i = 0; i < sizeof(pts) / sizeof(pts[0]); i++) {
		lla_to_ecef(pts[i][0], pts[i][1], pts[i][2], &x, &y, &z);
		if (x - pts[i][3] > 2 || pts[i][3] - x > 2 || y - pts[i][4] > 2 || pts[i][4] - y > 2 || z - pts[i][5] > 2 ||
		    pts[i][5] - z > 2)
			bad |= 4;
	}
	if (gps_time(1700000000u, 18, 0, &w, &t) || w != 2288 || t != 252818)
		bad |= 8;
	binit(&o);
	bs(&o, bad ? "selftest FAIL 0x" : "selftest ok 0x");
	bx(&o, bad, 2);
	bs(&o, "\n");
	putb(&o);
	return bad ? 5 : 0;
}

static int open_port(const char *path, int set4800, int binary)
{
	u32 t[15];
	struct buf o;

	binit(&o);
	port = sc(SYS_open, (long)path, 1 | 0x100 | 0x800, 0, 0, 0, 0);	/* O_WRONLY|O_NOCTTY|O_NONBLOCK */
	if (port < 0) {
		put("error: cannot open port\n");
		return 4;
	}
	if (sc(SYS_ioctl, port, TCGETS, (long)t, 0, 0, 0) < 0) {
		put("error: TCGETS failed (not a tty?)\n");
		return 4;
	}
	if (set4800) {
		t[0] = t[1] = t[3] = 0;
		t[2] = B4800 | 0x30 | 0x80 | 0x800;	/* B4800 | CS8 | CREAD | CLOCAL, as homtom-statusd */
		if (sc(SYS_ioctl, port, TCSETS, (long)t, 0, 0, 0) < 0) {
			put("error: TCSETS failed\n");
			return 4;
		}
	}
	bs(&o, "port speed code 0x");
	bx(&o, t[2] & CBAUD, 4);
	bs(&o, "\n");
	putb(&o);
	if ((t[2] & CBAUD) != B4800) {
		put("refused: port is not at 4800 baud; is homtom-statusd running? (--set-4800 only when nothing else owns it)\n");
		return 3;
	}
	if (binary && (t[1] & OPOST)) {
		put("refused: output post-processing (OPOST) is on, it would rewrite 0x0A bytes inside SiRF binary frames\n");
		return 3;
	}
	return 0;
}

int main_c(long *sp)
{
	int argc = (int)sp[0];
	char **argv = (char **)(sp + 1);
	const char *path = "/dev/ttySAC2", *mode = "nmea104", *tm = 0;
	struct aid a;
	struct buf o;
	int i, do_send = 0, set4800 = 0, to_nmea = 0, have_lat = 0, have_lon = 0, r;
	s32 leap = 18, woff = 0, v;
	u32 unix_s = 0;
	long it[4] = { 0, 0, 30, 0 };

	binit(&o);
	a.lat_udeg = a.lon_udeg = a.alt_m = 0;
	a.drift_hz = 0;
	a.channels = 12;
	a.hot = 0;
	for (i = 1; i < argc; i++) {
		const char *k = argv[i], *val = i + 1 < argc ? argv[i + 1] : 0;

		if (streq(k, "--selftest"))
			return selftest();
		else if (streq(k, "--send"))
			do_send = 1;
		else if (streq(k, "--set-4800"))
			set4800 = 1;
		else if (streq(k, "--to-nmea"))
			to_nmea = 1;
		else if (!val) {
			usage();
			return 2;
		} else {
			i++;
			if (streq(k, "--port"))
				path = val;
			else if (streq(k, "--mode"))
				mode = val;
			else if (streq(k, "--time"))
				tm = val;
			else if (streq(k, "--reset")) {
				if (streq(val, "hot-init"))
					a.hot = 1;
				else if (!streq(val, "warm-init")) {
					usage();
					return 2;
				}
			} else {
				int places = streq(k, "--lat") || streq(k, "--lon") ? 6 : 0;

				if (parse_fixed(val, places, &v)) {
					put("error: bad number\n");
					return 2;
				}
				if (streq(k, "--lat")) {
					a.lat_udeg = v;
					have_lat = 1;
				} else if (streq(k, "--lon")) {
					a.lon_udeg = v;
					have_lon = 1;
				} else if (streq(k, "--alt"))
					a.alt_m = v;
				else if (streq(k, "--leap"))
					leap = v;
				else if (streq(k, "--week-offset"))
					woff = v;
				else if (streq(k, "--drift"))
					a.drift_hz = v;
				else if (streq(k, "--channels"))
					a.channels = (u32)v;
				else {
					usage();
					return 2;
				}
			}
		}
	}

	sc(SYS_setitimer, 0, (long)it, 0, 0, 0, 0);	/* ITIMER_REAL 30 s: SIGALRM ends a stalled run */

	if (!to_nmea) {
		if (!have_lat || !have_lon || !tm) {
			usage();
			return 2;
		}
		if (a.lat_udeg < -90000000 || a.lat_udeg > 90000000 || a.lon_udeg < -180000000 || a.lon_udeg > 180000000 ||
		    a.alt_m < -500 || a.alt_m > 9000 || a.channels < 1 || a.channels > 12 || leap < 0 || leap > 60 ||
		    a.drift_hz < 0 || a.drift_hz > 200000) {
			put("refused: value out of range (alt -500..9000 m, channels 1..12, leap 0..60, drift 0..200000)\n");
			return 3;
		}
		if (streq(tm, "now")) {
			long tv[2] = { 0, 0 };

			sc(SYS_gettimeofday, (long)tv, 0, 0, 0, 0, 0);
			unix_s = (u32)tv[0];
		} else if (parse_fixed(tm, 0, &v) == 0)
			unix_s = (u32)v;
		if (gps_time(unix_s, leap, woff, &a.week, &a.tow_s) || unix_s < 1600000000u || a.week < 1024 ||
		    a.week > 4095) {
			put("refused: time before 2020 or week outside 1024..4095\n");
			return 3;
		}
		if (!streq(mode, "nmea104") && !streq(mode, "nmea101") && !streq(mode, "sirf")) {
			usage();
			return 2;
		}
		lla_to_ecef(a.lat_udeg, a.lon_udeg, a.alt_m, &a.x, &a.y, &a.z);
		bs(&o, "aid: unix ");
		bu(&o, unix_s);
		bs(&o, " week ");
		bu(&o, a.week);
		bs(&o, " tow ");
		bu(&o, a.tow_s);
		bs(&o, " ecef_m ");
		bi(&o, a.x);
		bs(&o, ",");
		bi(&o, a.y);
		bs(&o, ",");
		bi(&o, a.z);
		bs(&o, a.hot ? " hot-init\n" : " warm-init\n");
		putb(&o);
	}

	if (do_send) {
		r = open_port(path, set4800, to_nmea || streq(mode, "sirf"));
		if (r)
			return r;
	} else
		put("dry run: nothing is sent\n");

	r = 0;
	if (to_nmea) {
		r |= emit_sirf("sirf: MID 129 switch to NMEA 4800 (stock payload)", mid129_stock, 24);
	} else if (streq(mode, "nmea104")) {
		psrf104(&a, &o);
		r |= emit_nmea(&o);
	} else if (streq(mode, "nmea101")) {
		psrf101(&a, &o);
		r |= emit_nmea(&o);
	} else {
		u8 p[25];

		psrf100_sirf4800(&o);
		r |= emit_nmea(&o);
		if (do_send)
			msleep(1000);
		mid128(&a, p);
		r |= emit_sirf("sirf: MID 128 initialize data source", p, 25);
		if (do_send)
			msleep(2000);
		r |= emit_sirf("sirf: MID 129 switch to NMEA 4800 (stock payload)", mid129_stock, 24);
		if (do_send)
			msleep(1000);
		r |= emit_sirf("sirf: MID 129 again", mid129_stock, 24);
	}
	if (port >= 0)
		sc(SYS_close, port, 0, 0, 0, 0, 0);
	if (r) {
		put("error: write failed\n");
		return 4;
	}
	put(do_send ? "sent\n" : "done\n");
	return 0;
}

__asm__(".globl _start\n_start:\n mov r0, sp\n bl main_c\n mov r7, #1\n svc 0\n");
#endif
