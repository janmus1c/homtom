/* homtom-touch: resistive touch toolkit for the TomTom GO 630 (S3C2443).
 *
 * Hardware (stock drivers/barcelona/adc/adc.c:308-365, tomtomgo-iomodena.h:45-46,127-131, tomtomgo-iopins.h:42-43):
 *   GPG12 XPON  open emitter:  active = drive high, inactive = FLOAT (pin set to input)
 *   GPG13 XMON  open collector: active = drive low,  inactive = FLOAT
 *   GPG14 YPON  open emitter
 *   GPG15 YMON/TSDOWN open collector
 *   pen-down: ADCTSC 0x50 (pull-up on), all off, TSDOWN low, mux 9; stock "pressed" = below 100
 *   X: ADCTSC 0x58, XPON high + XMON low, mux 5.   Y: ADCTSC 0x58, YPON high + YMON low, mux 9
 *   ADCDLY 0xf000, prescaler 255.  Both earlier samplers drove inactive pins to the opposite level
 *   instead of floating them; "z" in a pin string below means float, as stock does.
 *
 * Pin strings are four characters for GPG12..15 (XPON XMON YPON YMON), each 0, 1 or z.
 * Pen-down is self-calibrating: the idle level of the detect channel is learned at start and a deviation
 * beyond an adaptive margin counts as pressed (direction auto, down or up).
 *
 * Freestanding ARMv4T EABI, raw syscalls, /dev/mem. No libc, no libgcc; division is udiv()/udiv64().
 * Build: arm-linux-gnueabi-gcc -nostdlib -march=armv4t -marm -static -fno-stack-protector -O1 \
 *          -o homtom-touch homtom-touch.c
 * Run "homtom-touch help" for modes and options.
 */
typedef unsigned int u32;
typedef int s32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef long long s64;
typedef unsigned long long u64;

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

#define SYS_EXIT	1
#define SYS_READ	3
#define SYS_WRITE	4
#define SYS_OPEN	5
#define SYS_CLOSE	6
#define SYS_ACCESS	33
#define SYS_SYNC	36
#define SYS_RENAME	38
#define SYS_MKDIR	39
#define SYS_IOCTL	54
#define SYS_FSYNC	118
#define SYS_NANOSLEEP	162
#define SYS_MMAP2	192
#define SYS_CLOCK_GETTIME 263

#define O_RDONLY	0
#define O_WRONLY	1
#define O_RDWR		2
#define O_CREAT		0x40
#define O_TRUNC		0x200
#define O_APPEND	0x400
#define O_NONBLOCK	0x800
#define O_SYNC		0x101000

#define IS_ERR(p)	((p) < 0 && (p) > -4096)

#define GPG_CON		(0x60 / 4)
#define GPG_DAT		(0x64 / 4)
#define GPG_UDP		(0x68 / 4)
#define ADCCON		(0x00 / 4)
#define ADCTSC		(0x04 / 4)
#define ADCDLY		(0x08 / 4)
#define ADCDAT0		(0x0c / 4)
#define ADCMUX		(0x18 / 4)
#define PCLKCON		(0x34 / 4)

#define W	480
#define H	272
#define FB_PHYS	0x33F00000u
#define FB_LEN	262144		/* 480*272*2 = 261120, rounded up to pages; stride 960 bytes */

#define CAL_DIR		"/mnt/internal/homtom"
#define CAL_FILE	"/mnt/internal/homtom/touch.cal"
#define CAL_TMP		"/mnt/internal/homtom/touch.cal.new"
#define CAL_GUARD	"/mnt/internal/ttsystem.lab2613"
#define CAL_RAM		"/tmp/touch.cal"
#define TEST_LOG	"/tmp/touch-test.log"
#define SCAN_LOG	"/tmp/touch-scan.log"

#define C_BLACK		0x0000
#define C_WHITE		0xffff
#define C_RED		0xf800
#define C_GREEN		0x07e0
#define C_BLUE		0x001f
#define C_YELLOW	0xffe0
#define C_GRAY		0x8410
#define C_DGRAY		0x39e7
#define C_CYAN		0x07ff

/* ---------------- arithmetic without libgcc ---------------- */
static u32 udiv(u32 n, u32 d)
{
	u32 q = 0, b = 1;

	if (!d)
		return 0;
	while (!(d & 0x80000000u) && d < n) {
		d <<= 1;
		b <<= 1;
	}
	while (b) {
		if (n >= d) {
			n -= d;
			q |= b;
		}
		d >>= 1;
		b >>= 1;
	}
	return q;
}

static s32 sdiv(s32 n, s32 d)
{
	int neg = 0;
	s32 q;

	if (n < 0) {
		n = -n;
		neg ^= 1;
	}
	if (d < 0) {
		d = -d;
		neg ^= 1;
	}
	q = (s32)udiv((u32)n, (u32)d);
	return neg ? -q : q;
}

static u64 udiv64(u64 n, u64 d)
{
	u64 q = 0, b = 1;

	if (!d)
		return 0;
	while (!(d >> 63) && d < n) {
		d <<= 1;
		b <<= 1;
	}
	while (b) {
		if (n >= d) {
			n -= d;
			q |= b;
		}
		d >>= 1;
		b >>= 1;
	}
	return q;
}

/* rounds to nearest */
static s64 sdiv64(s64 n, s64 d)
{
	int neg = 0;
	u64 q;

	if (n < 0) {
		n = -n;
		neg ^= 1;
	}
	if (d < 0) {
		d = -d;
		neg ^= 1;
	}
	q = udiv64((u64)n + ((u64)d >> 1), (u64)d);
	return neg ? -(s64)q : (s64)q;
}

static s32 iabs(s32 v)
{
	return v < 0 ? -v : v;
}

static u32 isqrt(u32 v)
{
	u32 r = 0, b = 1u << 30;

	while (b > v)
		b >>= 2;
	while (b) {
		if (v >= r + b) {
			v -= r + b;
			r = (r >> 1) + b;
		} else {
			r >>= 1;
		}
		b >>= 2;
	}
	return r;
}

static s32 clamp(s32 v, s32 lo, s32 hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

/* ---------------- strings and output ---------------- */
static int slen(const char *s)
{
	int n = 0;

	while (s[n])
		n++;
	return n;
}

static int streq(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

/* decimal, negative decimal or 0x hex */
static int num(const char *s, s32 *out)
{
	s32 v = 0;
	int neg = 0, any = 0;

	if (*s == '-') {
		neg = 1;
		s++;
	}
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		for (s += 2; *s; s++, any = 1) {
			if (*s >= '0' && *s <= '9')
				v = (v << 4) | (*s - '0');
			else if (*s >= 'a' && *s <= 'f')
				v = (v << 4) | (*s - 'a' + 10);
			else if (*s >= 'A' && *s <= 'F')
				v = (v << 4) | (*s - 'A' + 10);
			else
				return 0;
		}
	} else {
		for (; *s; s++, any = 1) {
			if (*s < '0' || *s > '9')
				return 0;
			v = v * 10 + (*s - '0');
		}
	}
	*out = neg ? -v : v;
	return any;
}

static int num64(const char *s, s64 *out)
{
	s64 v = 0;
	int neg = 0, any = 0;

	if (*s == '-') {
		neg = 1;
		s++;
	}
	for (; *s; s++, any = 1) {
		if (*s < '0' || *s > '9')
			return 0;
		v = v * 10 + (*s - '0');
	}
	*out = neg ? -v : v;
	return any;
}

#define TO_OUT	1
#define TO_KMSG	2
#define TO_LOG	4
#define TO_ERR	8

static char ob[640];
static int obn;
static long kfd = -1, lfd = -1;
static int info_to = TO_OUT;

static void oc(char c)
{
	if (obn < (int)sizeof(ob) - 2)
		ob[obn++] = c;
}

static void os(const char *s)
{
	while (*s)
		oc(*s++);
}

static void od64(s64 v)
{
	char t[24];
	int k = 0;
	u64 r;

	if (v < 0) {
		oc('-');
		r = (u64)(-v);
	} else {
		r = (u64)v;
	}
	do {
		u64 q = udiv64(r, 10);

		t[k++] = '0' + (char)(r - q * 10);
		r = q;
	} while (r);
	while (k)
		oc(t[--k]);
}

static void od(s32 v)
{
	char t[12];
	int k = 0;
	u32 r;

	if (v < 0) {
		oc('-');
		r = (u32)(-v);
	} else {
		r = (u32)v;
	}
	do {
		u32 q = udiv(r, 10);

		t[k++] = '0' + (char)(r - q * 10);
		r = q;
	} while (r);
	while (k)
		oc(t[--k]);
}

static void ox(u32 v)
{
	int i, started = 0;

	os("0x");
	for (i = 28; i >= 0; i -= 4) {
		u32 d = (v >> i) & 15;

		if (d || started || i == 0) {
			oc(d < 10 ? '0' + d : 'a' + d - 10);
			started = 1;
		}
	}
}

static void emit(int to)
{
	static char kb[700];
	static const char pfx[] = "homtom-touch: ";
	int i, n = 0;

	ob[obn++] = '\n';
	if (to & TO_OUT)
		sc(SYS_WRITE, 1, (long)ob, obn, 0, 0, 0);
	if (to & TO_ERR)
		sc(SYS_WRITE, 2, (long)ob, obn, 0, 0, 0);
	if ((to & TO_LOG) && lfd >= 0)
		sc(SYS_WRITE, lfd, (long)ob, obn, 0, 0, 0);
	if ((to & TO_KMSG) && kfd >= 0) {
		for (i = 0; pfx[i]; i++)
			kb[n++] = pfx[i];
		for (i = 0; i < obn; i++)
			kb[n++] = ob[i];
		sc(SYS_WRITE, kfd, (long)kb, n, 0, 0, 0);
	}
	obn = 0;
}

/* NUL-terminated view of the buffer; the buffer is reset, so use the result before the next oc() */
static const char *otake(void)
{
	ob[obn] = 0;
	obn = 0;
	return ob;
}

static void say(const char *s, int to)
{
	os(s);
	emit(to);
}

/* ---------------- time ---------------- */
static void msleep(u32 ms)
{
	long ts[2];
	u32 s = udiv(ms, 1000);

	ts[0] = (long)s;
	ts[1] = (long)((ms - s * 1000) * 1000000);
	sc(SYS_NANOSLEEP, (long)ts, 0, 0, 0, 0, 0);
}

static u32 now_ms(void)
{
	long ts[2];

	sc(SYS_CLOCK_GETTIME, 1, (long)ts, 0, 0, 0, 0);
	return (u32)ts[0] * 1000 + udiv((u32)ts[1], 1000000);
}

/* ---------------- configuration ---------------- */
#define PH_DET	0
#define PH_X	1
#define PH_Y	2

static u32 tsc[3] = { 0x50, 0x58, 0x58 };
static u32 mux[3] = { 9, 5, 9 };
static char pins[3][5] = { "zzz0", "10zz", "zz10" };
static u32 rmux[3] = { 9, 5, 7 };
static u32 settle = 3, avg = 4, discard = 1, presc = 255, dly = 0xf000;
static s32 udp = -2;			/* -2 stock (bit 0 set per pin), -1 keep, 0..3 force */
static int dir;				/* 0 auto, 1 down (value drops when pressed), 2 up */
static s32 opt_margin = -1, margin_min = 8, mul = 2, opt_base = -1;
static u32 learn_n = 40, interval = 100, opt_timeout = 120, tol = 12, maxerr = 15, spread = 40;
static u32 jitter = 2, rounds = 8;
static int opt_kmsg, opt_uinput, opt_force, opt_unmute, opt_nocal;
static const char *cal_path = CAL_FILE;

static volatile u32 *gpio, *adc;
static u32 clashes, timeouts;

/* ---------------- ADC and panel switching ---------------- */
static void set_pins(const char *p)
{
	u32 dat = gpio[GPG_DAT], con = gpio[GPG_CON];
	int i;

	for (i = 0; i < 4; i++) {
		u32 b = 12 + i;

		if (p[i] == '1')
			dat |= 1u << b;
		else
			dat &= ~(1u << b);
	}
	gpio[GPG_DAT] = dat;
	for (i = 0; i < 4; i++) {
		u32 sh = (12 + i) * 2;

		con &= ~(3u << sh);
		if (p[i] == '0' || p[i] == '1')
			con |= 1u << sh;
	}
	gpio[GPG_CON] = con;
}

/* one conversion; waits for the start bit to clear so a stale ECFLG is not taken; retried if another
 * ADC user (homtom-hw from the status loop) changed ADCMUX in between */
static u32 conv(u32 m)
{
	int t, i;
	u32 v = 0;

	for (t = 0; t < 4; t++) {
		adc[ADCMUX] = m;
		adc[ADCCON] = (1u << 14) | ((presc & 0xff) << 6) | 1u;
		for (i = 0; i < 200000 && (adc[ADCCON] & 1u); i++)
			;
		for (i = 0; i < 200000 && !(adc[ADCCON] & 0x8000u); i++)
			;
		if (i >= 200000)
			timeouts++;
		v = adc[ADCDAT0] & 0x3ff;
		if ((adc[ADCMUX] & 0xf) == m)
			return v;
		clashes++;
	}
	return v;
}

static s32 rd(u32 m)
{
	u32 i, s = 0;

	for (i = 0; i < discard; i++)
		conv(m);
	for (i = 0; i < avg; i++)
		s += conv(m);
	return (s32)udiv(s, avg ? avg : 1);
}

static void phase(int ph)
{
	adc[ADCTSC] = tsc[ph];
	set_pins(pins[ph]);
	if (settle)
		msleep(settle);
}

/* ---------------- self-calibrating pen-down ---------------- */
static s32 base16, margin, noise;
static int tdown;

static s32 dev_of(s32 v)
{
	s32 d = v - (base16 >> 4);

	if (dir == 1)
		return -d;
	if (dir == 2)
		return d;
	return iabs(d);
}

static const char *dirname(void)
{
	return dir == 1 ? "down" : dir == 2 ? "up" : "auto";
}

static void learn(void)
{
	u32 i, sum = 0;
	s32 lo = 1023, hi = 0, mean;

	for (i = 0; i < learn_n; i++) {
		s32 v;

		phase(PH_DET);
		v = rd(mux[PH_DET]);
		sum += v;
		if (v < lo)
			lo = v;
		if (v > hi)
			hi = v;
		msleep(15);
	}
	mean = (s32)udiv(sum, learn_n ? learn_n : 1);
	noise = hi - mean > mean - lo ? hi - mean : mean - lo;
	base16 = (opt_base >= 0 ? opt_base : mean) << 4;
	margin = opt_margin > 0 ? opt_margin : noise * mul + 4;
	if (margin < margin_min)
		margin = margin_min;
	os("learn mux=");
	od(mux[PH_DET]);
	os(" tsc=");
	ox(tsc[PH_DET]);
	os(" pins=");
	os(pins[PH_DET]);
	os(" base=");
	od(base16 >> 4);
	os(" min=");
	od(lo);
	os(" max=");
	od(hi);
	os(" noise=");
	od(noise);
	os(" margin=");
	od(margin);
	os(" dir=");
	os(dirname());
	emit(info_to | TO_LOG);
	if (noise > 40)
		say("warn: idle detect channel is noisy (>40 counts); check pins/tsc or keep hands off", info_to);
	if ((base16 >> 4) < margin && dir != 2)
		say("warn: idle sits near 0: a press can only show as a rise (try --dir up)", info_to);
	if ((base16 >> 4) > 1023 - margin && dir != 1)
		say("warn: idle sits near 1023: a press can only show as a drop (try --dir down)", info_to);
}

/* one sample frame: 1 when the detect channel says pressed before and after reading X and Y */
static int frame(s32 *x, s32 *y, s32 *z)
{
	s32 v, d, thr = tdown ? margin >> 1 : margin;

	phase(PH_DET);
	v = rd(mux[PH_DET]);
	d = dev_of(v);
	if (d <= thr) {
		if (!tdown && d < (margin >> 1))
			base16 += ((v << 4) - base16) >> 5;	/* follow slow drift while idle */
		return 0;
	}
	phase(PH_X);
	*x = rd(mux[PH_X]);
	phase(PH_Y);
	*y = rd(mux[PH_Y]);
	phase(PH_DET);
	v = rd(mux[PH_DET]);
	d = dev_of(v);
	if (d <= thr)
		return 0;
	*z = d;
	return 1;
}

/* ---------------- calibration (tslib pointercal layout: sx = (A*x + B*y + C) / S) ---------------- */
static s64 cal[7];
static int have_cal;

#define EV_NONE	0
#define EV_DOWN	1
#define EV_MOVE	2
#define EV_UP	3

static s32 trx, try_, tsx, tsy, tz;
static int dn_cnt, up_cnt;

static void to_screen(s32 rx, s32 ry, s32 *sx, s32 *sy)
{
	if (!have_cal || !cal[6]) {
		*sx = rx;
		*sy = ry;
		return;
	}
	*sx = clamp((s32)sdiv64(cal[0] * rx + cal[1] * ry + cal[2], cal[6]), 0, W - 1);
	*sy = clamp((s32)sdiv64(cal[3] * rx + cal[4] * ry + cal[5], cal[6]), 0, H - 1);
}

/* debounced touch state machine: 2 pressed frames for down, 2 released frames for up */
static int tpoll(void)
{
	s32 x, y, z;

	if (frame(&x, &y, &z)) {
		up_cnt = 0;
		if (!tdown) {
			if (++dn_cnt < 2)
				return EV_NONE;
			tdown = 1;
			trx = x;
			try_ = y;
			tz = z;
			to_screen(trx, try_, &tsx, &tsy);
			return EV_DOWN;
		}
		trx = (trx + x) >> 1;
		try_ = (try_ + y) >> 1;
		tz = z;
		to_screen(trx, try_, &tsx, &tsy);
		return EV_MOVE;
	}
	dn_cnt = 0;
	if (tdown && ++up_cnt >= 2) {
		tdown = 0;
		up_cnt = 0;
		return EV_UP;
	}
	return EV_NONE;
}

/* ---------------- framebuffer ---------------- */
static volatile u16 *fb;
static int muted;

static void mute(int on)
{
	long fd = sc(SYS_OPEN, (long)"/sys/kernel/homtom_splash", O_WRONLY, 0, 0, 0, 0);

	if (fd < 0) {
		say("warn: /sys/kernel/homtom_splash missing; console text may overwrite the drawing", info_to);
		return;
	}
	sc(SYS_WRITE, fd, (long)(on ? "1\n" : "0\n"), 2, 0, 0, 0);
	sc(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
	muted = on;
}

static void fill(s32 x, s32 y, s32 w, s32 h, u16 c)
{
	s32 i, j;

	if (!fb)
		return;
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (x + w > W)
		w = W - x;
	if (y + h > H)
		h = H - y;
	for (j = 0; j < h; j++) {
		volatile u16 *p = fb + (y + j) * W + x;

		for (i = 0; i < w; i++)
			p[i] = c;
	}
}

static void line(s32 x0, s32 y0, s32 x1, s32 y1, s32 r, u16 c)
{
	s32 dx = iabs(x1 - x0), dy = -iabs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;

	for (;;) {
		fill(x0 - r, y0 - r, 2 * r + 1, 2 * r + 1, c);
		if (x0 == x1 && y0 == y1)
			break;
		if (2 * e >= dy) {
			e += dy;
			x0 += sx;
		}
		if (2 * e <= dx) {
			e += dx;
			y0 += sy;
		}
	}
}

static void cross(s32 x, s32 y, u16 c)
{
	fill(x - 14, y - 1, 29, 3, c);
	fill(x - 1, y - 14, 3, 29, c);
}

/* 5x7 font, column bytes, bit 0 = top row */
static const char glyph_chars[] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-./:%!=,()";
static const u8 glyphs[][5] = {
	{ 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x3e, 0x51, 0x49, 0x45, 0x3e }, { 0x00, 0x42, 0x7f, 0x40, 0x00 }, { 0x42, 0x61, 0x51, 0x49, 0x46 },
	{ 0x21, 0x41, 0x45, 0x4b, 0x31 }, { 0x18, 0x14, 0x12, 0x7f, 0x10 }, { 0x27, 0x45, 0x45, 0x45, 0x39 },
	{ 0x3c, 0x4a, 0x49, 0x49, 0x30 }, { 0x01, 0x71, 0x09, 0x05, 0x03 }, { 0x36, 0x49, 0x49, 0x49, 0x36 },
	{ 0x06, 0x49, 0x49, 0x29, 0x1e },
	{ 0x7e, 0x11, 0x11, 0x11, 0x7e }, { 0x7f, 0x49, 0x49, 0x49, 0x36 }, { 0x3e, 0x41, 0x41, 0x41, 0x22 },
	{ 0x7f, 0x41, 0x41, 0x22, 0x1c }, { 0x7f, 0x49, 0x49, 0x49, 0x41 }, { 0x7f, 0x09, 0x09, 0x09, 0x01 },
	{ 0x3e, 0x41, 0x49, 0x49, 0x7a }, { 0x7f, 0x08, 0x08, 0x08, 0x7f }, { 0x00, 0x41, 0x7f, 0x41, 0x00 },
	{ 0x20, 0x40, 0x41, 0x3f, 0x01 }, { 0x7f, 0x08, 0x14, 0x22, 0x41 }, { 0x7f, 0x40, 0x40, 0x40, 0x40 },
	{ 0x7f, 0x02, 0x0c, 0x02, 0x7f }, { 0x7f, 0x04, 0x08, 0x10, 0x7f }, { 0x3e, 0x41, 0x41, 0x41, 0x3e },
	{ 0x7f, 0x09, 0x09, 0x09, 0x06 }, { 0x3e, 0x41, 0x51, 0x21, 0x5e }, { 0x7f, 0x09, 0x19, 0x29, 0x46 },
	{ 0x46, 0x49, 0x49, 0x49, 0x31 }, { 0x01, 0x01, 0x7f, 0x01, 0x01 }, { 0x3f, 0x40, 0x40, 0x40, 0x3f },
	{ 0x1f, 0x20, 0x40, 0x20, 0x1f }, { 0x3f, 0x40, 0x38, 0x40, 0x3f }, { 0x63, 0x14, 0x08, 0x14, 0x63 },
	{ 0x07, 0x08, 0x70, 0x08, 0x07 }, { 0x61, 0x51, 0x49, 0x45, 0x43 },
	{ 0x08, 0x08, 0x08, 0x08, 0x08 }, { 0x00, 0x60, 0x60, 0x00, 0x00 }, { 0x20, 0x10, 0x08, 0x04, 0x02 },
	{ 0x00, 0x36, 0x36, 0x00, 0x00 }, { 0x23, 0x13, 0x08, 0x64, 0x62 }, { 0x00, 0x00, 0x5f, 0x00, 0x00 },
	{ 0x14, 0x14, 0x14, 0x14, 0x14 }, { 0x00, 0x50, 0x30, 0x00, 0x00 }, { 0x00, 0x1c, 0x22, 0x41, 0x00 },
	{ 0x00, 0x41, 0x22, 0x1c, 0x00 },
};

static void text(s32 x, s32 y, const char *s, u16 c, s32 k)
{
	for (; *s; s++, x += 6 * k) {
		char ch = *s >= 'a' && *s <= 'z' ? *s - 32 : *s;
		int g, col, row;

		for (g = 0; glyph_chars[g] && glyph_chars[g] != ch; g++)
			;
		if (!glyph_chars[g])
			g = 0;
		for (col = 0; col < 5; col++)
			for (row = 0; row < 7; row++)
				if (glyphs[g][col] & (1 << row))
					fill(x + col * k, y + row * k, k, k, c);
	}
}

/* one text line of 2x glyphs across the screen, background cleared */
static void label(s32 y, const char *s, u16 c)
{
	fill(0, y, W, 18, C_BLACK);
	text(8, y + 2, s, c, 2);
}

static int fb_init(void)
{
	long fd, p;

	mute(1);
	fd = sc(SYS_OPEN, (long)"/dev/mem", O_RDWR | O_SYNC, 0, 0, 0, 0);
	if (fd < 0)
		return 0;
	p = sc(SYS_MMAP2, 0, FB_LEN, 3, 1, fd, FB_PHYS >> 12);
	if (IS_ERR(p)) {
		say("error: cannot map the framebuffer at 0x33f00000", info_to);
		return 0;
	}
	fb = (volatile u16 *)p;
	fill(0, 0, W, H, C_BLACK);
	return 1;
}

/* ---------------- logging ---------------- */
static void open_kmsg(void)
{
	if (kfd < 0)
		kfd = sc(SYS_OPEN, (long)"/dev/kmsg", O_WRONLY, 0, 0, 0, 0);
}

static void open_log(const char *path)
{
	lfd = sc(SYS_OPEN, (long)path, O_WRONLY | O_CREAT | O_APPEND, 0644, 0, 0, 0);
}

/* the hardware options in effect, as option tokens (stored in touch.cal so a later run reuses them) */
static void ocfg(void)
{
	os("--tsc-det ");
	ox(tsc[0]);
	os(" --tsc-x ");
	ox(tsc[1]);
	os(" --tsc-y ");
	ox(tsc[2]);
	os(" --mux-det ");
	od(mux[0]);
	os(" --mux-x ");
	od(mux[1]);
	os(" --mux-y ");
	od(mux[2]);
	os(" --det ");
	os(pins[0]);
	os(" --px ");
	os(pins[1]);
	os(" --py ");
	os(pins[2]);
	os(" --settle ");
	od(settle);
	os(" --avg ");
	od(avg);
	os(" --discard ");
	od(discard);
	os(" --presc ");
	od(presc);
	os(" --dly ");
	ox(dly);
	os(" --dir ");
	os(dirname());
}

static void tool_exit(int code)
{
	if (adc) {
		adc[ADCTSC] = 0x58;
		set_pins("zzzz");
	}
	if (opt_unmute && muted)
		mute(0);
	if (clashes || timeouts) {
		os("note: adc clashes=");
		od(clashes);
		os(" timeouts=");
		od(timeouts);
		emit(info_to);
	}
	sc(SYS_EXIT, code, 0, 0, 0, 0, 0);
}

/* ---------------- uinput ---------------- */
#define UI_SET_EVBIT	0x40045564
#define UI_SET_KEYBIT	0x40045565
#define UI_SET_ABSBIT	0x40045567
#define UI_SET_PROPBIT	0x4004556e
#define UI_DEV_CREATE	0x5501
#define EV_SYN		0
#define EV_KEY		1
#define EV_ABS		3
#define BTN_TOUCH	0x14a

static long ufd = -1;
static u32 udev_buf[1116 / 4];	/* uinput_user_dev: name[80], input_id, ff_effects_max, absmax/min/fuzz/flat[64] */

static void ui_open(void)
{
	static const char nm[] = "homtom-touch";
	char *p = (char *)udev_buf;
	int i;

	ufd = sc(SYS_OPEN, (long)"/dev/uinput", O_WRONLY | O_NONBLOCK, 0, 0, 0, 0);
	if (ufd < 0) {
		say("warn: /dev/uinput unavailable (insmod uinput.ko); continuing without it", info_to);
		return;
	}
	sc(SYS_IOCTL, ufd, UI_SET_EVBIT, EV_KEY, 0, 0, 0);
	sc(SYS_IOCTL, ufd, UI_SET_EVBIT, EV_ABS, 0, 0, 0);
	sc(SYS_IOCTL, ufd, UI_SET_KEYBIT, BTN_TOUCH, 0, 0, 0);
	sc(SYS_IOCTL, ufd, UI_SET_ABSBIT, 0, 0, 0, 0);
	sc(SYS_IOCTL, ufd, UI_SET_ABSBIT, 1, 0, 0, 0);
	sc(SYS_IOCTL, ufd, UI_SET_PROPBIT, 1, 0, 0, 0);		/* INPUT_PROP_DIRECT */
	for (i = 0; nm[i]; i++)
		p[i] = nm[i];
	*(u16 *)(p + 80) = 0x19;				/* BUS_HOST */
	udev_buf[92 / 4 + 0] = have_cal ? W - 1 : 1023;		/* absmax[ABS_X] */
	udev_buf[92 / 4 + 1] = have_cal ? H - 1 : 1023;		/* absmax[ABS_Y] */
	sc(SYS_WRITE, ufd, (long)udev_buf, sizeof(udev_buf), 0, 0, 0);
	if (sc(SYS_IOCTL, ufd, UI_DEV_CREATE, 0, 0, 0, 0) < 0) {
		say("warn: UI_DEV_CREATE failed; continuing without uinput", info_to);
		ufd = -1;
	}
}

static void ev(u32 type, u32 code, s32 val)
{
	u32 e[4];

	if (ufd < 0)
		return;
	e[0] = 0;
	e[1] = 0;
	e[2] = type | (code << 16);
	e[3] = (u32)val;
	sc(SYS_WRITE, ufd, (long)e, 16, 0, 0, 0);
}

/* ---------------- cal file ---------------- */
static char cbuf[1024];
static int parse_opts(int n, char **v);

static int load_cal(const char *path)
{
	static char *tok[64];
	long fd = sc(SYS_OPEN, (long)path, O_RDONLY, 0, 0, 0, 0), n;
	int nt = 0, i, o;

	if (fd < 0)
		return 0;
	n = sc(SYS_READ, fd, (long)cbuf, sizeof(cbuf) - 1, 0, 0, 0);
	sc(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
	if (n <= 0)
		return 0;
	cbuf[n] = 0;
	for (i = 0; i < n && nt < 64;) {
		while (i < n && (cbuf[i] == ' ' || cbuf[i] == '\n' || cbuf[i] == '\t' || cbuf[i] == '\r'))
			cbuf[i++] = 0;
		if (i >= n)
			break;
		tok[nt++] = cbuf + i;
		while (i < n && cbuf[i] != ' ' && cbuf[i] != '\n' && cbuf[i] != '\t' && cbuf[i] != '\r')
			i++;
	}
	if (nt < 7)
		return 0;
	for (i = 0; i < 7; i++)
		if (!num64(tok[i], &cal[i]))
			return 0;
	if (!cal[6])
		return 0;
	have_cal = 1;
	for (o = 7; o < nt && !streq(tok[o], "opts"); o++)
		;
	if (o < nt && parse_opts(nt - o - 1, tok + o + 1) < 0)
		say("warn: options stored in the cal file were not understood", info_to);
	os("cal loaded from ");
	os(path);
	emit(info_to);
	return 1;
}

static int write_file(const char *path, const char *data, int len)
{
	long fd = sc(SYS_OPEN, (long)path, O_WRONLY | O_CREAT | O_TRUNC, 0644, 0, 0, 0);
	long w;

	if (fd < 0)
		return 0;
	w = sc(SYS_WRITE, fd, (long)data, len, 0, 0, 0);
	sc(SYS_FSYNC, fd, 0, 0, 0, 0, 0);
	sc(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
	return w == len;
}

/* least-squares affine fit of n points; coefficients scaled by 65536 */
static int fit(int n, const s32 *rx, const s32 *ry, const s32 *px, const s32 *py, s64 *c)
{
	s64 sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0, cxx, cyy, cxy, det;
	int i, k;

	for (i = 0; i < n; i++) {
		sx += rx[i];
		sy += ry[i];
		sxx += (s64)rx[i] * rx[i];
		syy += (s64)ry[i] * ry[i];
		sxy += (s64)rx[i] * ry[i];
	}
	cxx = n * sxx - sx * sx;
	cyy = n * syy - sy * sy;
	cxy = n * sxy - sx * sy;
	det = cxx * cyy - cxy * cxy;
	if (det <= 0)
		return 0;
	for (k = 0; k < 2; k++) {
		const s32 *t = k ? py : px;
		s64 st = 0, sxt = 0, syt = 0, cxt, cyt, na, nb, d;

		for (i = 0; i < n; i++) {
			st += t[i];
			sxt += (s64)rx[i] * t[i];
			syt += (s64)ry[i] * t[i];
		}
		cxt = n * sxt - sx * st;
		cyt = n * syt - sy * st;
		na = cxt * cyy - cyt * cxy;
		nb = cyt * cxx - cxt * cxy;
		d = det;
		while (na > (1LL << 46) || na < -(1LL << 46) || nb > (1LL << 46) || nb < -(1LL << 46)) {
			na >>= 1;
			nb >>= 1;
			d >>= 1;
		}
		if (!d)
			return 0;
		c[k * 3 + 0] = sdiv64(na << 16, d);
		c[k * 3 + 1] = sdiv64(nb << 16, d);
		c[k * 3 + 2] = sdiv64((st << 16) - c[k * 3 + 0] * sx - c[k * 3 + 1] * sy, n);
	}
	c[6] = 65536;
	return 1;
}

/* ---------------- tap helper ---------------- */
/* waits for one press-and-release; returns 0 on timeout. Position = mean of the press after 2 settle samples. */
static int tap(s32 *hx, s32 *hy, s32 *rxo, s32 *ryo, s32 *sprd, u32 tmo)
{
	u32 t0 = now_ms();
	s32 sx = 0, sy = 0, rsx = 0, rsy = 0, n = 0, k = 0, lox = 9999, hix = -9999, loy = 9999, hiy = -9999;

	for (;;) {
		int e = tpoll();

		if (e == EV_DOWN) {
			sx = sy = rsx = rsy = n = k = 0;
			lox = loy = 9999;
			hix = hiy = -9999;
		}
		if ((e == EV_DOWN || e == EV_MOVE) && k++ >= 2) {
			sx += tsx;
			sy += tsy;
			rsx += trx;
			rsy += try_;
			n++;
			if (trx < lox)
				lox = trx;
			if (trx > hix)
				hix = trx;
			if (try_ < loy)
				loy = try_;
			if (try_ > hiy)
				hiy = try_;
		}
		if (e == EV_UP) {
			if (!n) {
				*hx = tsx;
				*hy = tsy;
				if (rxo) {
					*rxo = trx;
					*ryo = try_;
				}
			} else {
				*hx = sdiv(sx, n);
				*hy = sdiv(sy, n);
				if (rxo) {
					*rxo = sdiv(rsx, n);
					*ryo = sdiv(rsy, n);
				}
			}
			if (sprd)
				*sprd = n < 6 ? -1 : (hix - lox > hiy - loy ? hix - lox : hiy - loy);
			return 1;
		}
		if (!tdown && now_ms() - t0 > tmo)
			return 0;
		msleep(4);
	}
}

static void need_cal(void)
{
	if (!have_cal)
		say("warn: no calibration loaded; coordinates are raw ADC counts (run cal first)", info_to | TO_KMSG);
}

/* ---------------- modes ---------------- */
static void read9(s32 *v)
{
	int p, c;

	for (p = 0; p < 3; p++) {
		phase(p);
		for (c = 0; c < 3; c++)
			v[p * 3 + c] = rd(rmux[c]);
	}
}

static void mode_raw(void)
{
	s32 v[9], idle[9], last[9];
	int i, r, st = 0, tick = 0;

	if (opt_kmsg)
		open_kmsg();
	say("raw: keep hands off the screen while the idle level is learned", info_to | (opt_kmsg ? TO_KMSG : 0));
	learn();
	for (i = 0; i < 9; i++)
		idle[i] = last[i] = 0;
	for (r = 0; r < 8; r++) {
		read9(v);
		for (i = 0; i < 9; i++)
			idle[i] += v[i];
	}
	for (i = 0; i < 9; i++)
		idle[i] >>= 3;
	os("# ts det/X/Y phases, each mux ");
	od(rmux[0]);
	oc('/');
	od(rmux[1]);
	oc('/');
	od(rmux[2]);
	os("; det=detect channel, p=deviation from learned idle, l1=sum of |reading-idle| over the 9; ");
	ocfg();
	emit(info_to);
	for (;;) {
		s32 dv, p, l1 = 0, moved = 0, thr;

		read9(v);
		phase(PH_DET);
		dv = rd(mux[PH_DET]);
		p = dev_of(dv);
		thr = st ? margin >> 1 : margin;
		st = p > thr;
		for (i = 0; i < 9; i++) {
			l1 += iabs(v[i] - idle[i]);
			if (iabs(v[i] - last[i]) > 6)
				moved = 1;
		}
		os("ts ");
		for (i = 0; i < 9; i++) {
			od(v[i]);
			oc(i == 2 || i == 5 || i == 8 ? ' ' : '/');
		}
		os("det=");
		od(dv);
		os(" base=");
		od(base16 >> 4);
		os(" p=");
		od(p);
		os(" l1=");
		od(l1);
		os(st ? " DOWN" : " up");
		if (opt_kmsg && (moved || ++tick >= 25)) {
			emit(info_to | TO_KMSG);
			for (i = 0; i < 9; i++)
				last[i] = v[i];
			tick = 0;
		} else {
			emit(info_to);
		}
		if (!st && p < (margin >> 1))
			base16 += ((dv << 4) - base16) >> 5;
		msleep(interval);
	}
}

static void mode_events(void)
{
	s32 lx = -1000, ly = -1000;

	info_to = TO_ERR;
	if (opt_kmsg)
		open_kmsg();
	need_cal();
	learn();
	if (opt_uinput)
		ui_open();
	for (;;) {
		int e = tpoll();

		if (e == EV_DOWN || (e == EV_MOVE && (iabs(tsx - lx) > (s32)jitter || iabs(tsy - ly) > (s32)jitter))) {
			od(tsx);
			oc(' ');
			od(tsy);
			os(e == EV_DOWN ? " down" : " move");
			emit(TO_OUT | (e == EV_DOWN && opt_kmsg ? TO_KMSG : 0));
			lx = tsx;
			ly = tsy;
			ev(EV_ABS, 0, tsx);
			ev(EV_ABS, 1, tsy);
			if (e == EV_DOWN)
				ev(EV_KEY, BTN_TOUCH, 1);
			ev(EV_SYN, 0, 0);
		} else if (e == EV_UP) {
			od(lx);
			oc(' ');
			od(ly);
			os(" up");
			emit(TO_OUT | (opt_kmsg ? TO_KMSG : 0));
			ev(EV_KEY, BTN_TOUCH, 0);
			ev(EV_SYN, 0, 0);
			lx = ly = -1000;
		}
		msleep(tdown ? 2 : 8);
	}
}

static void handsoff(void)
{
	label(70, "HANDS OFF - LEARNING IDLE", C_YELLOW);
	learn();
	label(70, "", C_BLACK);
}

#define CAL_N	5
static const s32 cal_px[CAL_N] = { 30, W - 31, W - 31, 30, W / 2 };
static const s32 cal_py[CAL_N] = { 30, 30, H - 31, H - 31, H / 2 };

static void mode_cal(void)
{
	s32 rx[CAL_N], ry[CAL_N], i, worst = 0, minx = 9999, maxx = -9999, miny = 9999, maxy = -9999;
	s64 c[7];
	static char file[512];
	int fl, j;
	const char *s;

	open_kmsg();
	open_log(TEST_LOG);
	if (!fb_init())
		tool_exit(1);
	have_cal = 0;
	handsoff();
	for (i = 0; i < CAL_N; i++) {
		s32 hx, hy, sp;

		cross(cal_px[i], cal_py[i], C_WHITE);
		os("TOUCH THE CROSS ");
		od(i + 1);
		os("/5");
		label(110, otake(), C_WHITE);
		for (;;) {
			if (!tap(&hx, &hy, &rx[i], &ry[i], &sp, opt_timeout * 1000)) {
				say("cal: timeout waiting for a touch", info_to | TO_KMSG);
				tool_exit(3);
			}
			if (sp < 0) {
				label(110, "HOLD A LITTLE LONGER", C_YELLOW);
				continue;
			}
			if (sp > (s32)spread) {
				label(110, "HOLD STILL - AGAIN", C_YELLOW);
				continue;
			}
			break;
		}
		cross(cal_px[i], cal_py[i], C_GREEN);
		os("cal point ");
		od(i + 1);
		os(" screen=");
		od(cal_px[i]);
		oc(',');
		od(cal_py[i]);
		os(" raw=");
		od(rx[i]);
		oc(',');
		od(ry[i]);
		os(" spread=");
		od(sp);
		emit(info_to | TO_LOG);
		if (rx[i] < minx)
			minx = rx[i];
		if (rx[i] > maxx)
			maxx = rx[i];
		if (ry[i] < miny)
			miny = ry[i];
		if (ry[i] > maxy)
			maxy = ry[i];
	}
	if (maxx - minx < 60 && maxy - miny < 60)
		say("warn: raw readings barely move across the screen; X/Y wiring or options are suspect", info_to | TO_KMSG);
	if (!fit(CAL_N, rx, ry, cal_px, cal_py, c)) {
		label(110, "CAL FAILED - DEGENERATE", C_RED);
		say("cal: degenerate points, no fit", info_to | TO_KMSG | TO_LOG);
		tool_exit(4);
	}
	for (j = 0; j < 7; j++)
		cal[j] = c[j];
	have_cal = 1;
	for (i = 0; i < CAL_N; i++) {
		s32 sx, sy, e;

		to_screen(rx[i], ry[i], &sx, &sy);
		e = (s32)isqrt((u32)((sx - cal_px[i]) * (sx - cal_px[i]) + (sy - cal_py[i]) * (sy - cal_py[i])));
		if (e > worst)
			worst = e;
		fill(sx - 2, sy - 2, 5, 5, e <= (s32)maxerr ? C_CYAN : C_RED);
		os("cal residual point ");
		od(i + 1);
		os(" mapped=");
		od(sx);
		oc(',');
		od(sy);
		os(" err=");
		od(e);
		emit(info_to | TO_LOG);
	}
	for (j = 0; j < 7; j++) {
		od64(c[j]);
		oc(' ');
	}
	os("480 272\nopts ");
	ocfg();
	oc('\n');
	fl = obn;
	for (j = 0; j < fl; j++)
		file[j] = ob[j];
	obn = 0;
	os("cal result maxerr=");
	od(worst);
	os(" coeffs ");
	for (j = 0; j < 7; j++) {
		od64(c[j]);
		oc(' ');
	}
	emit(info_to | TO_KMSG | TO_LOG);
	write_file(CAL_RAM, file, fl);
	if (worst > (s32)maxerr && !opt_force) {
		os("CAL BAD: ERROR ");
		od(worst);
		os(" PX - RUN AGAIN");
		label(110, otake(), C_RED);
		say("cal: residual above --maxerr; not written to INTERNAL (copy kept in /tmp/touch.cal; --force writes anyway)",
		    info_to | TO_KMSG | TO_LOG);
		tool_exit(4);
	}
	if (sc(SYS_ACCESS, (long)CAL_GUARD, 0, 0, 0, 0, 0) != 0) {
		label(110, "NOT SAVED: INTERNAL NOT FOUND", C_RED);
		say("cal: /mnt/internal/ttsystem.lab2613 missing (not INTERNAL?); kept /tmp/touch.cal only",
		    info_to | TO_KMSG | TO_LOG);
		tool_exit(5);
	}
	sc(SYS_MKDIR, (long)CAL_DIR, 0755, 0, 0, 0, 0);
	if (!write_file(CAL_TMP, file, fl) || sc(SYS_RENAME, (long)CAL_TMP, (long)CAL_FILE, 0, 0, 0, 0) != 0) {
		label(110, "WRITE FAILED", C_RED);
		say("cal: writing " CAL_FILE " failed; kept /tmp/touch.cal", info_to | TO_KMSG | TO_LOG);
		tool_exit(5);
	}
	sc(SYS_SYNC, 0, 0, 0, 0, 0, 0);
	os("CAL SAVED, MAX ERROR ");
	od(worst);
	os(" PX");
	s = otake();
	label(110, s, C_GREEN);
	say("cal: saved " CAL_FILE, info_to | TO_KMSG | TO_LOG);
	tool_exit(0);
}

static void result_line(const char *test, int ok)
{
	os(ok ? " PASS" : " FAIL");
	emit(info_to | TO_KMSG | TO_LOG);
	(void)test;
}

static void graphics_start(void)
{
	open_kmsg();
	open_log(TEST_LOG);
	need_cal();
	if (!fb_init())
		tool_exit(1);
	handsoff();
}

static const s32 tgt[9][2] = {
	{ 30, 30 }, { W / 2, 30 }, { W - 31, 30 },
	{ 30, H / 2 }, { W / 2, H / 2 }, { W - 31, H / 2 },
	{ 30, H - 31 }, { W / 2, H - 31 }, { W - 31, H - 31 },
};

static void mode_targets(void)
{
	int i, pass = 0;
	s32 worst = 0;

	graphics_start();
	for (i = 0; i < 9; i++) {
		s32 cx = tgt[i][0], cy = tgt[i][1], hx, hy, e;
		int ok;

		fill(cx - 20, cy - 20, 40, 40, C_YELLOW);
		fill(cx - 1, cy - 1, 3, 3, C_BLACK);
		os("TAP THE SQUARE ");
		od(i + 1);
		os("/9");
		label(70, otake(), C_WHITE);
		os("targets n=");
		od(i + 1);
		os(" want=");
		od(cx);
		oc(',');
		od(cy);
		if (!tap(&hx, &hy, 0, 0, 0, 20000)) {
			fill(cx - 20, cy - 20, 40, 40, C_RED);
			os(" timeout");
			result_line("targets", 0);
			worst = 9999;
			continue;
		}
		e = (s32)isqrt((u32)((hx - cx) * (hx - cx) + (hy - cy) * (hy - cy)));
		ok = e <= (s32)tol;
		pass += ok;
		if (e > worst)
			worst = e;
		fill(cx - 20, cy - 20, 40, 40, ok ? C_GREEN : C_RED);
		cross(hx, hy, C_WHITE);
		os(" got=");
		od(hx);
		oc(',');
		od(hy);
		os(" err=");
		od(e);
		result_line("targets", ok);
	}
	os("targets summary pass=");
	od(pass);
	os("/9 maxerr=");
	od(worst);
	os(" tol=");
	od(tol);
	result_line("targets", pass == 9);
	os("TARGETS ");
	od(pass);
	os("/9 MAX ERR ");
	od(worst);
	label(110, otake(), pass == 9 ? C_GREEN : C_RED);
	tool_exit(pass == 9 ? 0 : 6);
}

static const s32 dpath[3][4] = {
	{ 40, 40, W - 41, H - 41 },
	{ 40, H - 41, W - 41, 40 },
	{ 40, H / 2, W - 41, H / 2 },
};

static void mode_drag(void)
{
	int p, pass = 0;

	graphics_start();
	for (p = 0; p < 3; p++) {
		s32 x0 = dpath[p][0], y0 = dpath[p][1], x1 = dpath[p][2], y1 = dpath[p][3];
		s32 dx = x1 - x0, dy = y1 - y0, len = (s32)isqrt((u32)(dx * dx + dy * dy));
		s32 maxdev = 0, maxt = -9999, maxjump = 0, n = 0, lx = 0, ly = 0, sd = 0, ed = 0;
		u32 t0;
		int e, ok, started = 0;

		fill(0, 0, W, H, C_BLACK);
		line(x0, y0, x1, y1, 4, C_DGRAY);
		fill(x0 - 8, y0 - 8, 17, 17, C_GREEN);
		fill(x1 - 8, y1 - 8, 17, 17, C_RED);
		label(H - 20, "DRAG GREEN TO RED, ONE STROKE", C_WHITE);
		t0 = now_ms();
		for (;;) {
			e = tpoll();
			if (e == EV_DOWN) {
				started = 1;
				sd = (s32)isqrt((u32)((tsx - x0) * (tsx - x0) + (tsy - y0) * (tsy - y0)));
				lx = tsx;
				ly = tsy;
			}
			if (e == EV_DOWN || e == EV_MOVE) {
				s32 cr = dx * (tsy - y0) - dy * (tsx - x0), t = dx * (tsx - x0) + dy * (tsy - y0);
				s32 dv = sdiv(iabs(cr), len), j = (s32)isqrt((u32)((tsx - lx) * (tsx - lx) + (tsy - ly) * (tsy - ly)));

				t = sdiv(t, len);
				if (dv > maxdev)
					maxdev = dv;
				if (t > maxt)
					maxt = t;
				if (j > maxjump)
					maxjump = j;
				line(lx, ly, tsx, tsy, 1, C_WHITE);
				fill(tsx - 1, tsy - 1, 3, 3, C_RED);
				lx = tsx;
				ly = tsy;
				n++;
			}
			if (e == EV_UP) {
				ed = (s32)isqrt((u32)((lx - x1) * (lx - x1) + (ly - y1) * (ly - y1)));
				break;
			}
			if (!started && now_ms() - t0 > 30000)
				break;
			msleep(3);
		}
		ok = started && sd <= 30 && ed <= 30 && maxt * 100 >= len * 90 && maxdev <= (s32)tol * 2 && maxjump <= 40;
		pass += ok;
		os("drag path=");
		od(p + 1);
		if (!started) {
			os(" timeout");
		} else {
			os(" samples=");
			od(n);
			os(" start_err=");
			od(sd);
			os(" end_err=");
			od(ed);
			os(" progress%=");
			od(sdiv(maxt * 100, len));
			os(" maxdev=");
			od(maxdev);
			os(" maxjump=");
			od(maxjump);
		}
		result_line("drag", ok);
		label(H - 20, ok ? "PASS" : "FAIL", ok ? C_GREEN : C_RED);
		msleep(1500);
	}
	os("drag summary pass=");
	od(pass);
	os("/3");
	result_line("drag", pass == 3);
	os("DRAG ");
	od(pass);
	os("/3");
	label(110, otake(), pass == 3 ? C_GREEN : C_RED);
	tool_exit(pass == 3 ? 0 : 6);
}

#define SL_X0	40
#define SL_X1	440
#define SL_Y	120
static void slider_draw(s32 knob, s32 target)
{
	fill(SL_X0 - 10, SL_Y - 10, SL_X1 - SL_X0 + 21, 51, C_BLACK);
	fill(SL_X0, SL_Y + 12, SL_X1 - SL_X0, 6, C_GRAY);
	fill(SL_X0 + (target << 2) - 2, SL_Y - 10, 5, 51, C_YELLOW);
	if (knob >= 0)
		fill(knob - 6, SL_Y, 13, 30, C_WHITE);
}

static void mode_slide(void)
{
	static const s32 st[3] = { 10, 90, 50 };
	int i, pass = 0;

	graphics_start();
	for (i = 0; i < 3; i++) {
		s32 knob = -1, val = -1;
		u32 t0 = now_ms();
		int e, ok, got = 0;

		slider_draw(knob, st[i]);
		os("SLIDE TO THE YELLOW MARK (");
		od(st[i]);
		os("), LIFT");
		label(70, otake(), C_WHITE);
		for (;;) {
			e = tpoll();
			if (e == EV_DOWN || e == EV_MOVE) {
				s32 k = clamp(tsx, SL_X0, SL_X1);

				if (k != knob) {
					knob = k;
					slider_draw(knob, st[i]);
				}
			}
			if (e == EV_UP && knob >= 0) {
				got = 1;
				break;
			}
			if (!tdown && now_ms() - t0 > 20000)
				break;
			msleep(3);
		}
		if (got)
			val = sdiv((knob - SL_X0) * 100, SL_X1 - SL_X0);
		ok = got && iabs(val - st[i]) <= 3;
		pass += ok;
		os("slide n=");
		od(i + 1);
		os(" want=");
		od(st[i]);
		os(got ? " got=" : " timeout");
		if (got)
			od(val);
		result_line("slide", ok);
		label(160, ok ? "PASS" : "FAIL", ok ? C_GREEN : C_RED);
		msleep(1000);
		label(160, "", C_BLACK);
	}
	os("slide summary pass=");
	od(pass);
	os("/3");
	result_line("slide", pass == 3);
	os("SLIDE ");
	od(pass);
	os("/3");
	label(70, otake(), pass == 3 ? C_GREEN : C_RED);
	tool_exit(pass == 3 ? 0 : 6);
}

static void draw_chrome(void)
{
	fill(0, 0, 60, 30, C_BLUE);
	text(12, 8, "CLR", C_WHITE, 2);
	fill(W - 60, 0, 60, 30, C_RED);
	text(W - 48, 8, "END", C_WHITE, 2);
}

static void mode_draw(void)
{
	s32 lx = 0, ly = 0, n = 0, maxjump = 0, strokes = 0;
	u32 last = now_ms();
	int ign = 0;

	graphics_start();
	draw_chrome();
	text(80, 8, "DRAW", C_GRAY, 2);
	for (;;) {
		int e = tpoll();

		if (e != EV_NONE)
			last = now_ms();
		if (e == EV_DOWN) {
			if (tsy < 32 && tsx >= W - 60)
				break;
			ign = tsy < 32 && tsx < 60;
			if (ign)
				fill(0, 32, W, H - 32, C_BLACK);
			lx = tsx;
			ly = tsy;
			n = 0;
			maxjump = 0;
		}
		if (ign) {
			if (e == EV_UP)
				ign = 0;
			msleep(3);
			continue;
		}
		if (e == EV_DOWN || e == EV_MOVE) {
			s32 j = (s32)isqrt((u32)((tsx - lx) * (tsx - lx) + (tsy - ly) * (tsy - ly)));

			if (j > maxjump)
				maxjump = j;
			if (j > 40) {
				os("draw jump ");
				od(j);
				os(" px from ");
				od(lx);
				oc(',');
				od(ly);
				os(" to ");
				od(tsx);
				oc(',');
				od(tsy);
				emit(info_to | TO_LOG);
			}
			line(lx, ly, tsx, tsy, 1, C_WHITE);
			fill(tsx - 1, tsy - 1, 2, 2, C_RED);
			lx = tsx;
			ly = tsy;
			n++;
		}
		if (e == EV_UP) {
			strokes++;
			os("draw stroke ");
			od(strokes);
			os(" samples=");
			od(n);
			os(" maxjump=");
			od(maxjump);
			os(" z=");
			od(tz);
			emit(info_to | TO_LOG);
		}
		if (!tdown && now_ms() - last > opt_timeout * 1000)
			break;
		msleep(3);
	}
	os("draw done strokes=");
	od(strokes);
	emit(info_to | TO_KMSG | TO_LOG);
	tool_exit(0);
}

/* scan: idle versus pressed for a list of switch configurations and ADC channels 5..9 */
#define NSC	12
#define NSM	5
static const char *const sc_name[NSC] = {
	"det-stock", "det-nopullup", "det-legacy", "det-allfloat", "det-xm-low",
	"x-stock", "x-pullup", "x-legacy", "y-stock", "y-pullup", "y-legacy", "z-xp-ym",
};
static const u32 sc_tsc[NSC] = { 0x50, 0x58, 0x50, 0x50, 0x50, 0x58, 0x50, 0x58, 0x58, 0x50, 0x58, 0x58 };
static const char *const sc_pins[NSC] = {
	"zzz0", "zzz0", "0100", "zzzz", "z0zz", "10zz", "10zz", "1001", "zz10", "zz10", "0110", "1zz0",
};
static s32 sc_idle[NSC][NSM], sc_press[NSC][NSM];

static void scan_pass(s32 acc[NSC][NSM])
{
	u32 r;
	int c, m;

	for (c = 0; c < NSC; c++)
		for (m = 0; m < NSM; m++)
			acc[c][m] = 0;
	for (r = 0; r < rounds; r++)
		for (c = 0; c < NSC; c++) {
			adc[ADCTSC] = sc_tsc[c];
			set_pins(sc_pins[c]);
			msleep(settle ? settle : 1);
			for (m = 0; m < NSM; m++)
				acc[c][m] += rd(5 + m);
		}
	for (c = 0; c < NSC; c++)
		for (m = 0; m < NSM; m++)
			acc[c][m] = sdiv(acc[c][m], rounds ? rounds : 1);
}

static void mode_scan(void)
{
	int c, m, s, hits = 0;

	open_kmsg();
	open_log(SCAN_LOG);
	say("scan: hands off the screen", info_to | TO_KMSG);
	msleep(1500);
	scan_pass(sc_idle);
	say("scan: PRESS AND HOLD the screen centre firmly (stylus or fingernail) until told to release", info_to | TO_KMSG);
	for (s = 3; s > 0; s--) {
		os("scan: measuring in ");
		od(s);
		emit(info_to | TO_KMSG);
		msleep(1000);
	}
	scan_pass(sc_press);
	say("scan: release now", info_to | TO_KMSG);
	os("# scan config tsc pins(XPON XMON YPON YMON) mux idle press delta; '*' = |delta| >= 40; ");
	os("settle=");
	od(settle);
	os(" avg=");
	od(avg);
	os(" presc=");
	od(presc);
	emit(info_to | TO_LOG);
	for (c = 0; c < NSC; c++)
		for (m = 0; m < NSM; m++) {
			s32 d = sc_press[c][m] - sc_idle[c][m];
			int big = iabs(d) >= 40;

			os("scan ");
			os(sc_name[c]);
			os(" tsc=");
			ox(sc_tsc[c]);
			os(" pins=");
			os(sc_pins[c]);
			os(" mux=");
			od(5 + m);
			os(" idle=");
			od(sc_idle[c][m]);
			os(" press=");
			od(sc_press[c][m]);
			os(" delta=");
			od(d);
			if (big)
				os(" *");
			hits += big;
			emit(info_to | TO_LOG | (big ? TO_KMSG : 0));
		}
	os("scan done: ");
	od(hits);
	os(" rows with |delta| >= 40 (log " SCAN_LOG ")");
	emit(info_to | TO_KMSG | TO_LOG);
	tool_exit(0);
}

/* host-free self test of the arithmetic and the fit (runs under qemu-arm, no /dev/mem) */
static void mode_selftest(void)
{
	static const s32 rx[CAL_N] = { 900, 120, 118, 905, 512 }, ry[CAL_N] = { 150, 148, 860, 862, 505 };
	s64 c[7];
	s32 i, worst = 0;
	int ok = 1;

	if (udiv(1000, 7) != 142 || sdiv(-1000, 7) != -142 || isqrt(1000000) != 1000 ||
	    sdiv64(-10000000000LL, 3) != -3333333333LL)
		ok = 0;
	if (!fit(CAL_N, rx, ry, cal_px, cal_py, c))
		ok = 0;
	for (i = 0; i < 7; i++)
		cal[i] = c[i];
	have_cal = 1;
	for (i = 0; i < CAL_N; i++) {
		s32 sx, sy, e;

		to_screen(rx[i], ry[i], &sx, &sy);
		e = iabs(sx - cal_px[i]) + iabs(sy - cal_py[i]);
		if (e > worst)
			worst = e;
	}
	os("selftest coeffs ");
	for (i = 0; i < 7; i++) {
		od64(c[i]);
		oc(' ');
	}
	os("worst=");
	od(worst);
	os(ok && worst <= 6 ? " PASS" : " FAIL");
	emit(TO_OUT);
	sc(SYS_EXIT, ok && worst <= 6 ? 0 : 1, 0, 0, 0, 0, 0);
}

/* ---------------- options ---------------- */
static const char help[] =
	"usage: homtom-touch MODE [options]\n"
	"modes: raw | scan | cal | draw | drag | slide | targets | events | selftest | help\n"
	"  raw      9 readings (det/X/Y phase x 3 muxes) + pressure every --interval ms\n"
	"  scan     idle vs pressed table for 12 switch configs x mux 5..9 (one press at the centre)\n"
	"  cal      5-point crosshair calibration -> " CAL_FILE "\n"
	"  draw     paint trail; CLR/END boxes top left/right\n"
	"  drag     drag along 3 paths; slide: slider to 3 marks; targets: tap 9 squares\n"
	"  events   'x y down|move|up' on stdout (diagnostics on stderr); --uinput adds an evdev device\n"
	"hardware (pins = GPG12..15 = XPON XMON YPON YMON, each 0/1/z, z = float):\n"
	"  --det zzz0 --px 10zz --py zz10   pin states for detect / X / Y phases\n"
	"  --xpon V --xmon V --ypon V --ymon V   override one pin in the detect phase\n"
	"  --tsc V (all phases) --tsc-det 0x50 --tsc-x 0x58 --tsc-y 0x58\n"
	"  --mux-det 9 --mux-x 5 --mux-y 9   --rmux 9,5,7 (raw columns)\n"
	"  --settle 3 (ms) --avg 4 --discard 1 --presc 255 --dly 0xf000 --udp stock|keep|0..3\n"
	"  --legacy   old k-mode setup (driven-off pins, tsc 0x50, presc 49, avg 1, discard 0)\n"
	"detect: --dir auto|down|up --margin N --margin-min 8 --mul 2 --base N --learn 40\n"
	"other: --interval 100 --kmsg --cal FILE --no-cal --uinput --jitter 2 --tol 12 --maxerr 15\n"
	"       --spread 40 --timeout 120 (s) --force --unmute --rounds 8\n";

static int bad(const char *s)
{
	os("bad option or value: ");
	os(s);
	emit(TO_ERR);
	return -1;
}

static int setpins(char *dst, const char *s)
{
	int i;

	if (slen(s) != 4)
		return 0;
	for (i = 0; i < 4; i++) {
		char ch = s[i] == 'Z' ? 'z' : s[i];

		if (ch != '0' && ch != '1' && ch != 'z')
			return 0;
		dst[i] = ch;
	}
	dst[4] = 0;
	return 1;
}

static int pinval(const char *s, char *dst)
{
	if (slen(s) != 1 || (s[0] != '0' && s[0] != '1' && s[0] != 'z' && s[0] != 'Z'))
		return 0;
	*dst = s[0] == 'Z' ? 'z' : s[0];
	return 1;
}

static int parse_opts(int n, char **v)
{
	int i;

	for (i = 0; i < n; i++) {
		const char *k = v[i], *a;
		s32 t = 0;

		if (streq(k, "--kmsg")) {
			opt_kmsg = 1;
			continue;
		}
		if (streq(k, "--uinput")) {
			opt_uinput = 1;
			continue;
		}
		if (streq(k, "--force")) {
			opt_force = 1;
			continue;
		}
		if (streq(k, "--unmute")) {
			opt_unmute = 1;
			continue;
		}
		if (streq(k, "--no-cal")) {
			opt_nocal = 1;
			continue;
		}
		if (streq(k, "--legacy")) {
			setpins(pins[0], "0100");
			setpins(pins[1], "1001");
			setpins(pins[2], "0110");
			tsc[0] = tsc[1] = tsc[2] = 0x50;
			presc = 49;
			avg = 1;
			discard = 0;
			udp = -1;
			continue;
		}
		if (i + 1 >= n)
			return bad(k);
		a = v[++i];
		if (streq(k, "--det") || streq(k, "--px") || streq(k, "--py")) {
			if (!setpins(pins[k[2] == 'd' ? 0 : k[3] == 'x' ? 1 : 2], a))
				return bad(a);
			continue;
		}
		if (streq(k, "--xpon") || streq(k, "--xmon") || streq(k, "--ypon") || streq(k, "--ymon")) {
			int idx = (k[2] == 'y' ? 2 : 0) + (k[3] == 'm' ? 1 : 0);

			if (!pinval(a, &pins[0][idx]))
				return bad(a);
			continue;
		}
		if (streq(k, "--dir")) {
			if (streq(a, "auto"))
				dir = 0;
			else if (streq(a, "down"))
				dir = 1;
			else if (streq(a, "up"))
				dir = 2;
			else
				return bad(a);
			continue;
		}
		if (streq(k, "--udp")) {
			if (streq(a, "keep"))
				udp = -1;
			else if (streq(a, "stock"))
				udp = -2;
			else if (num(a, &t) && t >= 0 && t <= 3)
				udp = t;
			else
				return bad(a);
			continue;
		}
		if (streq(k, "--cal")) {
			cal_path = a;
			continue;
		}
		if (streq(k, "--rmux")) {
			if (slen(a) < 5 || a[1] != ',' || a[3] != ',' || a[0] < '0' || a[0] > '9' || a[2] < '0' ||
			    a[2] > '9' || a[4] < '0' || a[4] > '9')
				return bad(a);
			rmux[0] = a[0] - '0';
			rmux[1] = a[2] - '0';
			rmux[2] = a[4] - '0';
			continue;
		}
		if (!num(a, &t) || t < 0)
			return bad(a);
		if (streq(k, "--tsc"))
			tsc[0] = tsc[1] = tsc[2] = t & 0x1ff;
		else if (streq(k, "--tsc-det"))
			tsc[0] = t & 0x1ff;
		else if (streq(k, "--tsc-x"))
			tsc[1] = t & 0x1ff;
		else if (streq(k, "--tsc-y"))
			tsc[2] = t & 0x1ff;
		else if (streq(k, "--mux-det") && t <= 15)
			mux[0] = t;
		else if (streq(k, "--mux-x") && t <= 15)
			mux[1] = t;
		else if (streq(k, "--mux-y") && t <= 15)
			mux[2] = t;
		else if (streq(k, "--settle"))
			settle = t;
		else if (streq(k, "--avg") && t >= 1 && t <= 64)
			avg = t;
		else if (streq(k, "--discard") && t <= 16)
			discard = t;
		else if (streq(k, "--presc") && t >= 1 && t <= 255)
			presc = t;
		else if (streq(k, "--dly"))
			dly = t & 0xffff;
		else if (streq(k, "--margin"))
			opt_margin = t;
		else if (streq(k, "--margin-min"))
			margin_min = t;
		else if (streq(k, "--mul"))
			mul = t;
		else if (streq(k, "--base") && t <= 1023)
			opt_base = t;
		else if (streq(k, "--learn") && t >= 4 && t <= 1000)
			learn_n = t;
		else if (streq(k, "--interval"))
			interval = t;
		else if (streq(k, "--jitter"))
			jitter = t;
		else if (streq(k, "--tol"))
			tol = t;
		else if (streq(k, "--maxerr"))
			maxerr = t;
		else if (streq(k, "--spread"))
			spread = t;
		else if (streq(k, "--timeout") && t >= 1)
			opt_timeout = t;
		else if (streq(k, "--rounds") && t >= 1 && t <= 100)
			rounds = t;
		else
			return bad(k);
	}
	return 0;
}

int main_c(long *sp)
{
	long argc = sp[0];
	char **argv = (char **)(sp + 1);
	const char *mode = argc > 1 ? argv[1] : "help";
	long fd, g, a, ck;
	int i;

	if (streq(mode, "help") || streq(mode, "-h") || streq(mode, "--help")) {
		sc(SYS_WRITE, 1, (long)help, sizeof(help) - 1, 0, 0, 0);
		return 0;
	}
	if (streq(mode, "selftest"))
		mode_selftest();
	if (streq(mode, "events"))
		info_to = TO_ERR;
	for (i = 2; i + 1 < argc; i++)
		if (streq(argv[i], "--cal"))
			cal_path = argv[i + 1];
	for (i = 2; i < argc; i++)
		if (streq(argv[i], "--no-cal"))
			opt_nocal = 1;
	if (!opt_nocal && !streq(mode, "cal") && !streq(mode, "scan"))
		load_cal(cal_path);
	if (parse_opts(argc - 2 > 0 ? argc - 2 : 0, argv + 2) < 0) {
		sc(SYS_WRITE, 2, (long)help, sizeof(help) - 1, 0, 0, 0);
		return 2;
	}

	fd = sc(SYS_OPEN, (long)"/dev/mem", O_RDWR | O_SYNC, 0, 0, 0, 0);
	if (fd < 0) {
		say("error: cannot open /dev/mem", TO_ERR);
		return 1;
	}
	g = sc(SYS_MMAP2, 0, 4096, 3, 1, fd, 0x56000);
	a = sc(SYS_MMAP2, 0, 4096, 3, 1, fd, 0x58000);
	ck = sc(SYS_MMAP2, 0, 4096, 1, 1, fd, 0x4c000);
	if (IS_ERR(g) || IS_ERR(a)) {
		say("error: cannot map GPIO/ADC", TO_ERR);
		return 1;
	}
	gpio = (volatile u32 *)g;
	adc = (volatile u32 *)a;
	if (!IS_ERR(ck) && !(((volatile u32 *)ck)[PCLKCON] & (1u << 7)))
		say("warn: ADC clock gate (PCLKCON bit 7) is off; readings will be 0", info_to);

	os("cfg ");
	ocfg();
	os(" udp=");
	od(udp);
	os(" gpgcon=");
	ox(gpio[GPG_CON]);
	os(" gpgdat=");
	ox(gpio[GPG_DAT]);
	os(" gpgudp=");
	ox(gpio[GPG_UDP]);
	os(" cal=");
	od(have_cal);
	emit(info_to);

	if (udp == -2)
		gpio[GPG_UDP] |= 0x55000000u;		/* stock IOP_DisablePullResistor on 2443: bit 0 per pin */
	else if (udp >= 0)
		gpio[GPG_UDP] = (gpio[GPG_UDP] & 0x00ffffffu) | ((u32)udp * 0x55000000u);
	adc[ADCDLY] = dly;

	if (streq(mode, "raw"))
		mode_raw();
	else if (streq(mode, "scan"))
		mode_scan();
	else if (streq(mode, "cal"))
		mode_cal();
	else if (streq(mode, "draw"))
		mode_draw();
	else if (streq(mode, "drag"))
		mode_drag();
	else if (streq(mode, "slide"))
		mode_slide();
	else if (streq(mode, "targets"))
		mode_targets();
	else if (streq(mode, "events"))
		mode_events();
	say("unknown mode; try: homtom-touch help", TO_ERR);
	tool_exit(2);
	return 2;
}

__asm__(".globl _start\n_start:\n mov r0, sp\n bl main_c\n mov r7, #1\n svc 0\n");
