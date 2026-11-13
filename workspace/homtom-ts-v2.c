/* homtom-ts v2: resistive touch for the GO 630 (S3C2443), modelled on stock drivers/barcelona/adc/adc.c.
 * Pen-down: ADCTSC 0x50 (pull-up on), GPG15 low, mux 9; pressed when below the threshold (stock 100).
 * X: ADCTSC 0x58, XPON (GPG12) high + XMON (GPG13) low, mux 5.  Y: ADCTSC 0x58, YPON (GPG14) high + YMON (GPG15) low, mux 9.
 * Each read: one discarded conversion, then 4 averaged; output is (last+new)/2 as in tsinput.c.
 * Freestanding ARMv4T EABI, /dev/mem. No libc, no libgcc: division is udiv() below.
 * Build: arm-linux-gnueabi-gcc -march=armv4t -marm -Os -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns
 *        -nostdlib -static -o homtom-ts2 homtom-ts-v2.c
 * usage: homtom-ts2 MODE [THR] [X0 X1 Y0 Y1 SWAP]
 *   MODE letters: r = raw "z x y" every frame; c = 4-corner calibration, prints "cal X0 X1 Y0 Y1 SWAP";
 *                 t = touch, prints "x y" (screen 480x272 with cal, raw without) and "up";
 *                 k = also log to /dev/kmsg (rate-limited); u = also inject evdev events through /dev/uinput.
 *   THR: pen-down threshold on mux 9 (default 100). Check it in r mode first.
 *   e.g. homtom-ts2 r ; homtom-ts2 c ; homtom-ts2 tku 100 912 120 860 150 0 */
typedef unsigned int u32;
typedef int s32;
typedef unsigned short u16;

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
#define SYS_WRITE	4
#define SYS_OPEN	5
#define SYS_IOCTL	54
#define SYS_NANOSLEEP	162
#define SYS_MMAP2	192

#define GPG_DAT	(0x64 / 4)
#define GPG_CON	(0x60 / 4)
#define GPG_UDP	(0x68 / 4)
#define ADCCON	(0x00 / 4)
#define ADCTSC	(0x04 / 4)
#define ADCDLY	(0x08 / 4)
#define ADCDAT0	(0x0c / 4)
#define ADCMUX	(0x18 / 4)

#define ADCCON_V	((1u << 14) | (255u << 6))	/* prescaler on, /256 as stock (adc.c:62) */
#define TSC_DOWN	0x50				/* XP/YP drivers off, pull-up on (adc.c:315) */
#define TSC_XY		0x58				/* XP/YP drivers off, pull-up off (adc.c:567) */

#define W	480
#define H	272
#define M	24					/* calibration target inset in pixels */

static volatile u32 *gpio, *adc;
static u32 thr = 100;
static long kfd = -1, ufd = -1;
static s32 cal[5];
static int have_cal;

static void msleep(long ms)
{
	long ts[2];

	ts[0] = 0;
	ts[1] = ms * 1000000;
	sc(SYS_NANOSLEEP, (long)ts, 0, 0, 0, 0, 0);
}

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

static s32 iabs(s32 v)
{
	return v < 0 ? -v : v;
}

static int pn(char *b, s32 v)
{
	char t[12];
	int k = 0, n = 0;
	u32 r;

	if (v < 0) {
		b[n++] = '-';
		v = -v;
	}
	r = (u32)v;
	do {
		u32 q = udiv(r, 10);

		t[k++] = '0' + (r - q * 10);
		r = q;
	} while (r);
	while (k)
		b[n++] = t[--k];
	return n;
}

static int ps(char *b, const char *s)
{
	int n = 0;

	while (s[n]) {
		b[n] = s[n];
		n++;
	}
	return n;
}

static s32 num(const char *s)
{
	s32 v = 0;
	int neg = 0;

	if (*s == '-') {
		neg = 1;
		s++;
	}
	while (*s >= '0' && *s <= '9')
		v = v * 10 + (*s++ - '0');
	return neg ? -v : v;
}

static int has(const char *s, char c)
{
	for (; *s; s++)
		if (*s == c)
			return 1;
	return 0;
}

/* line to stdout, and to /dev/kmsg when tok is set */
static void say(const char *b, int n, int tok)
{
	sc(SYS_WRITE, 1, (long)b, n, 0, 0, 0);
	if (tok && kfd >= 0)
		sc(SYS_WRITE, kfd, (long)b, n, 0, 0, 0);
}

/* GPG12-15 levels; polarity as in the working k mode (XPON/YPON high = on, XMON/YMON low = on) */
static void gpg(u32 xpon, u32 xmon, u32 ypon, u32 ymon)
{
	u32 v = gpio[GPG_DAT] & ~0xf000u;

	gpio[GPG_DAT] = v | (xpon << 12) | (xmon << 13) | (ypon << 14) | (ymon << 15);
}

/* one conversion: wait for the start bit to clear first, so a stale ECFLG from the previous conversion is not taken */
static u32 conv(u32 mux)
{
	int i;

	adc[ADCMUX] = mux;
	adc[ADCCON] = ADCCON_V | 1u;
	for (i = 0; i < 200000 && (adc[ADCCON] & 1u); i++)
		;
	for (i = 0; i < 200000 && !(adc[ADCCON] & 0x8000u); i++)
		;
	return adc[ADCDAT0] & 0x3ff;
}

static u32 rd(u32 mux)
{
	u32 s;

	conv(mux);
	s = conv(mux) + conv(mux) + conv(mux) + conv(mux);
	return s >> 2;
}

static u32 rd_down(void)
{
	adc[ADCTSC] = TSC_DOWN;
	gpg(0, 1, 0, 0);
	msleep(3);
	return rd(9);
}

static u32 rd_x(void)
{
	adc[ADCTSC] = TSC_XY;
	gpg(1, 0, 0, 1);
	msleep(3);
	return rd(5);
}

static u32 rd_y(void)
{
	adc[ADCTSC] = TSC_XY;
	gpg(0, 1, 1, 0);
	msleep(3);
	return rd(9);
}

/* 1 when the pen is down for the whole frame (checked before and after X/Y, so lift-off samples are dropped) */
static int frame(u32 *x, u32 *y)
{
	if (rd_down() >= thr)
		return 0;
	*x = rd_x();
	*y = rd_y();
	return rd_down() < thr;
}

/* ---- evdev through uinput (legacy uinput_user_dev setup, works on 6.1) ---- */
#define UI_SET_EVBIT	0x40045564
#define UI_SET_KEYBIT	0x40045565
#define UI_SET_ABSBIT	0x40045567
#define UI_SET_PROPBIT	0x4004556e
#define UI_DEV_CREATE	0x5501
#define EV_SYN	0
#define EV_KEY	1
#define EV_ABS	3
#define BTN_TOUCH	0x14a

static u32 udev[1116 / 4];	/* name[80], input_id, ff_effects_max, absmax/absmin/absfuzz/absflat[64] */

static void ui_open(void)
{
	const char *nm = "homtom-ts";
	char *p = (char *)udev;
	int i;

	ufd = sc(SYS_OPEN, (long)"/dev/uinput", 1 | 0x800, 0, 0, 0, 0);
	if (ufd < 0) {
		say("uinput unavailable (insmod uinput.ko)\n", 38, 0);
		return;
	}
	sc(SYS_IOCTL, ufd, UI_SET_EVBIT, EV_KEY, 0, 0, 0);
	sc(SYS_IOCTL, ufd, UI_SET_EVBIT, EV_ABS, 0, 0, 0);
	sc(SYS_IOCTL, ufd, UI_SET_KEYBIT, BTN_TOUCH, 0, 0, 0);
	sc(SYS_IOCTL, ufd, UI_SET_ABSBIT, 0, 0, 0, 0);
	sc(SYS_IOCTL, ufd, UI_SET_ABSBIT, 1, 0, 0, 0);
	sc(SYS_IOCTL, ufd, UI_SET_PROPBIT, 1, 0, 0, 0);	/* INPUT_PROP_DIRECT */
	for (i = 0; nm[i]; i++)
		p[i] = nm[i];
	*(u16 *)(p + 80) = 0x19;			/* BUS_HOST */
	udev[(92 / 4) + 0] = have_cal ? W - 1 : 1023;	/* absmax[ABS_X] */
	udev[(92 / 4) + 1] = have_cal ? H - 1 : 1023;	/* absmax[ABS_Y] */
	sc(SYS_WRITE, ufd, (long)udev, sizeof(udev), 0, 0, 0);
	sc(SYS_IOCTL, ufd, UI_DEV_CREATE, 0, 0, 0, 0);
}

static void ev(u32 type, u32 code, s32 val)
{
	static u32 e[4];

	e[0] = 0;
	e[1] = 0;
	e[2] = type | (code << 16);
	e[3] = (u32)val;
	sc(SYS_WRITE, ufd, (long)e, 16, 0, 0, 0);
}

/* ---- calibration ---- */
static s32 map(s32 raw, s32 r0, s32 r1, s32 s0, s32 s1, s32 max)
{
	s32 v;

	if (r1 == r0)
		return 0;
	v = s0 + sdiv((raw - r0) * (s1 - s0), r1 - r0);
	return v < 0 ? 0 : v > max ? max : v;
}

static void to_screen(u32 x, u32 y, s32 *sx, s32 *sy)
{
	s32 h = cal[4] ? (s32)y : (s32)x, v = cal[4] ? (s32)x : (s32)y;

	if (!have_cal) {
		*sx = x;
		*sy = y;
		return;
	}
	*sx = map(h, cal[0], cal[1], M, W - 1 - M, W - 1);
	*sy = map(v, cal[2], cal[3], M, H - 1 - M, H - 1);
}

static volatile u16 *fb;

static void cross(int cx, int cy, u16 c)
{
	int i;

	if (!fb)
		return;
	for (i = -8; i <= 8; i++) {
		if (cx + i >= 0 && cx + i < W)
			fb[cy * W + cx + i] = c;
		if (cy + i >= 0 && cy + i < H)
			fb[(cy + i) * W + cx] = c;
	}
}

static void grab(int cx, int cy, const char *name, u32 *ax, u32 *ay)
{
	char b[64];
	int n = 0, k = 0;
	u32 x, y, sx = 0, sy = 0;

	cross(cx, cy, 0xffff);
	n = ps(b, "touch ");
	n += ps(b + n, name);
	b[n++] = '\n';
	say(b, n, 1);
	while (k < 20) {			/* 4 settling frames, then 16 averaged; a lift restarts */
		if (!frame(&x, &y)) {
			k = 0;
			sx = sy = 0;
			continue;
		}
		if (k >= 4) {
			sx += x;
			sy += y;
		}
		k++;
	}
	*ax = sx >> 4;
	*ay = sy >> 4;
	cross(cx, cy, 0);
	for (k = 0; k < 10;)			/* wait for 10 consecutive up frames */
		k = frame(&x, &y) ? 0 : k + 1;
}

static void calibrate(void)
{
	u32 tlx, tly, trx, try_, blx, bly, brx, bry;
	s32 swap, h0, h1, v0, v1;
	char b[64];
	int n;
	long fd = sc(SYS_OPEN, (long)"/dev/fb0", 2, 0, 0, 0, 0);

	if (fd >= 0) {
		long p = sc(SYS_MMAP2, 0, W * H * 2, 3, 1, fd, 0);

		if (!(p < 0 && p > -4096))
			fb = (volatile u16 *)p;
	}
	grab(M, M, "top-left", &tlx, &tly);
	grab(W - 1 - M, M, "top-right", &trx, &try_);
	grab(M, H - 1 - M, "bottom-left", &blx, &bly);
	grab(W - 1 - M, H - 1 - M, "bottom-right", &brx, &bry);
	/* if mux 9 changes more than mux 5 along the top edge, the axes are swapped */
	swap = iabs((s32)try_ - (s32)tly) > iabs((s32)trx - (s32)tlx);
	if (swap) {
		h0 = (tly + bly) >> 1; h1 = (try_ + bry) >> 1;
		v0 = (tlx + trx) >> 1; v1 = (blx + brx) >> 1;
	} else {
		h0 = (tlx + blx) >> 1; h1 = (trx + brx) >> 1;
		v0 = (tly + try_) >> 1; v1 = (bly + bry) >> 1;
	}
	n = ps(b, "cal ");
	n += pn(b + n, h0); b[n++] = ' ';
	n += pn(b + n, h1); b[n++] = ' ';
	n += pn(b + n, v0); b[n++] = ' ';
	n += pn(b + n, v1); b[n++] = ' ';
	n += pn(b + n, swap); b[n++] = '\n';
	say(b, n, 1);
}

/* ---- modes ---- */
static void raw(void)
{
	char b[48];
	int n;

	for (;;) {
		u32 z = rd_down(), x = rd_x(), y = rd_y();

		n = pn(b, z); b[n++] = ' ';
		n += pn(b + n, x); b[n++] = ' ';
		n += pn(b + n, y); b[n++] = '\n';
		say(b, n, 0);
		msleep(100);
	}
}

static void touch(void)
{
	char b[48];
	int n, down = 0;
	u32 x, y, ax = 0, ay = 0;
	s32 lx = -100, ly = -100;

	for (;;) {
		s32 sx, sy;

		if (!frame(&x, &y)) {
			if (down) {
				say("up\n", 3, 1);
				if (ufd >= 0) {
					ev(EV_KEY, BTN_TOUCH, 0);
					ev(EV_SYN, 0, 0);
				}
				lx = ly = -100;
			}
			down = 0;
			msleep(10);
			continue;
		}
		if (!down) {			/* first frame after pen-down seeds the average */
			ax = x;
			ay = y;
		}
		ax = (ax + x) >> 1;
		ay = (ay + y) >> 1;
		to_screen(ax, ay, &sx, &sy);
		n = pn(b, sx); b[n++] = ' ';
		n += pn(b + n, sy); b[n++] = '\n';
		say(b, n, !down || iabs(sx - lx) > 6 || iabs(sy - ly) > 6);
		if (!down || iabs(sx - lx) > 6 || iabs(sy - ly) > 6) {
			lx = sx;
			ly = sy;
		}
		if (ufd >= 0) {
			ev(EV_ABS, 0, sx);
			ev(EV_ABS, 1, sy);
			if (!down)
				ev(EV_KEY, BTN_TOUCH, 1);
			ev(EV_SYN, 0, 0);
		}
		down = 1;
	}
}

int main_c(long *sp)
{
	long argc = sp[0];
	char **argv = (char **)(sp + 1);
	const char *mode = argc > 1 ? argv[1] : "t";
	long fd = sc(SYS_OPEN, (long)"/dev/mem", 2 | 0x101000, 0, 0, 0, 0);
	long g, a;
	int i;

	if (fd < 0)
		return 1;
	g = sc(SYS_MMAP2, 0, 4096, 3, 1, fd, 0x56000);
	a = sc(SYS_MMAP2, 0, 4096, 3, 1, fd, 0x58000);
	if ((g < 0 && g > -4096) || (a < 0 && a > -4096))
		return 1;
	gpio = (volatile u32 *)g;
	adc = (volatile u32 *)a;
	if (argc > 2)
		thr = num(argv[2]);
	if (argc > 7) {
		for (i = 0; i < 5; i++)
			cal[i] = num(argv[3 + i]);
		have_cal = 1;
	}
	gpio[GPG_CON] = (gpio[GPG_CON] & 0x00ffffffu) | 0x55000000u;	/* GPG12-15 outputs */
	gpio[GPG_UDP] = (gpio[GPG_UDP] & 0x00ffffffu) | 0x55000000u;	/* pulls off (tomtomgo-type.c:1484) */
	adc[ADCDLY] = 0x1000;						/* short; plane settling is the msleep(3) */
	if (has(mode, 'k'))
		kfd = sc(SYS_OPEN, (long)"/dev/kmsg", 1, 0, 0, 0, 0);
	if (has(mode, 'r'))
		raw();
	if (has(mode, 'c')) {
		calibrate();
		return 0;
	}
	if (has(mode, 'u'))
		ui_open();
	touch();
	return 0;
}

__asm__(".globl _start\n_start:\n mov r0, sp\n bl main_c\n mov r7, #1\n svc 0\n");
