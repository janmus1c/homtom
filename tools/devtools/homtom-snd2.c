/* homtom-snd2: WM8750 tone player with diagnostics, successor of tools/devtools/homtom-snd.c.
 *
 * Usage: homtom-snd2 [--hz N] [--secs N] [--vol 0-127] [--hold S] [--amp on|off|auto] [--iiscon 1|7] [--xr]
 *                    [--probe] [--mic S] [--micsel 0-3]
 *                    [--micmeter S] [--gain 0-63]
 *   --vol    raw LOUT1/ROUT1/LOUT2/ROUT2 value, 121 = 0 dB, 1 dB steps, below 48 = mute. Default 117 (stock INITIAL_VOLUME).
 *   --hold   keep codec, amp and IIS powered S seconds after the tone (read registers from another shell meanwhile).
 *   --amp    on (default): AMP_ON high regardless of the jack; auto: skip AMP_ON when GPG3 reads low AFTER DAC power-up.
 *   --iiscon value for IISCON while playing: 1 = I2SACTIVE only (default, PIO), 7 = stock (TX/RX DMA-active bits too).
 *   --xr     TXR = transmit+receive like stock; default is transmit only (RX FIFO otherwise sits full).
 *   --probe  FIFO acceptance and drain-rate test only: no codec writes, no amp, prints counts and frames/s.
 *   --mic S  capture S seconds per input select after the codec init, print peak and mean |x| per channel.
 *   --micmeter S  live level meter, S = 1..60 s: input select 1 (or --micsel 0-3), right channel, PGA --gain. Every 250 ms
 *            prints one line "m LEVEL PEAK LEFT" (whole dBFS, floor -90; LEFT = whole seconds still to run) after the
 *            window mean (the codec's DC offset) is removed; LEVEL is RMS (20*log10(rms/32768)), PEAK the largest sample.
 *            Normal priority, speaker amp never touched, power-down on exit. Maths: homtom-micmath.h.
 *
 * Evidence for the changes (S = .runtime/kernel-build, B = S/drivers/barcelona/sound):
 *   1. homtom-snd read HEADPHONE_DETECT GPG3 before DAC_PWR_ON. Measured: GPGDAT bit 3 = 0 with GPJ15 low, 1 with GPJ15 high
 *      (2026-11-07, pull-up enabled: GPGUDP pins 0-7 = 00 = pull-up per L/arch/arm/mach-s3c/gpio-samsung.c:69-83).
 *      So the old tool always took the "headphone" branch and never raised AMP_ON (GPGDAT bit 11 stayed 0 during the run).
 *      Stock raises AMP_ON unconditionally on open (B/driver.c:1536); the jack only matters to userland (driver.c:1356-1359).
 *   2. Volume is the raw register value: stock sets 117 (S/include/barcelona/Barc_snd.h:113, B/driver.c:1539-1540,
 *      B/wm8971.c:199-235). 20 is below the WM8750 mute threshold, 60 is about -61 dB.
 *   3. Measured: a 10 s tone returned in under 1 s and IISFIC TX count stayed 0 with IISCON FTXEMPT set during the run,
 *      so the FIFO never throttled the writer. --probe tells "writes dropped" from "drains too fast".
 * Register write rules from homtom-snd.c are unchanged: masked GPIO writes, never GPJDAT 10/14, GPJCON 10/14, any GPL,
 * EPLLCON or CLKSRC bits 6-8. Only CLKSRC[15:14], PCLKCON bit 9 and SCLKCON bit 9 are touched in the clock block.
 *
 * Build (project flags): arm-linux-gnueabi-gcc -nostdlib -march=armv4t -marm -static -fno-stack-protector -O1
 *   -fno-tree-loop-distribute-patterns homtom-snd2.c -o homtom-snd2 */
typedef unsigned int u32;
typedef unsigned short u16;
typedef short s16;

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

static u32 udiv(u32 n, u32 d)
{
	u32 q = 0, bit = 1;

	if (!d)
		return 0;
	while (d < n && !(d & 0x80000000u)) {
		d <<= 1;
		bit <<= 1;
	}
	while (bit) {
		if (n >= d) {
			n -= d;
			q |= bit;
		}
		d >>= 1;
		bit >>= 1;
	}
	return q;
}

static u32 slen(const char *s)
{
	u32 n = 0;

	while (s[n])
		n++;
	return n;
}

#include "homtom-micmath.h"

static u32 mic_gain = 0x3f, dump_raw;
static u32 rawbuf[49152];

static void out(const char *s)
{
	if (dump_raw)
		return;
	sc(4, 1, (long)s, slen(s), 0, 0, 0);
}

static void dec(u32 v)
{
	char b[12];
	int i = 11;

	b[i] = 0;
	do {
		b[--i] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	out(b + i);
}

static void hex(u32 v)
{
	char b[11];
	int i;

	b[0] = '0';
	b[1] = 'x';
	for (i = 0; i < 8; i++)
		b[2 + i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15];
	b[10] = 0;
	out(b);
}

static int eq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

static u32 num(const char *s)
{
	u32 v = 0;

	if (!s)
		return 0;
	for (; *s >= '0' && *s <= '9'; s++)
		v = v * 10 + (*s - '0');
	return v;
}

static void msleep(u32 ms)
{
	long ts[2] = { ms / 1000, (ms % 1000) * 1000000L };

	sc(162, (long)ts, 0, 0, 0, 0, 0);
}

static u32 now_us(void)
{
	long ts[2];

	sc(263, 1, (long)ts, 0, 0, 0, 0);	/* clock_gettime(CLOCK_MONOTONIC) */
	return (u32)ts[0] * 1000000u + (u32)ts[1] / 1000u;
}

static volatile u32 *map(long fd, u32 pa)
{
	long m = sc(192, 0, 4096, 3, 1, fd, pa >> 12);

	return (m < 0 && m > -4096) ? 0 : (volatile u32 *)m;
}

static volatile u32 *clk, *iis, *gpio;

static void wr(volatile u32 *p, u32 mask, u32 val)
{
	*p = (*p & ~mask) | (val & mask);
}

static int gw(u32 off, u32 mask, u32 val)
{
	if ((off == 0xD4 && (mask & 0x4400)) || (off == 0xD0 && (mask & 0x30300000)) || (off >= 0xF0 && off <= 0xFC))
		return -1;
	wr(gpio + off / 4, mask, val);
	return 0;
}

#define GPBCON 0x10
#define GPBDAT 0x14
#define GPECON 0x40
#define GPEDAT 0x44
#define GPGCON 0x60
#define GPGDAT 0x64
#define GPJCON 0xD0
#define GPJDAT 0xD4

#define IISCON (iis + 0)
#define IISMOD (iis + 1)
#define IISFIC (iis + 2)
#define IISPSR (iis + 3)
#define IISTXD (iis + 4)
#define IISRXD (iis + 5)

#define TXCNT() ((*IISFIC >> 8) & 0x1f)
#define RXCNT() (*IISFIC & 0x1f)
#define TXFULL() (*IISCON & (1u << 8))

static void dump(const char *tag)
{
	out("[");
	out(tag);
	out("] IISCON=");
	hex(*IISCON);
	out(" IISMOD=");
	hex(*IISMOD);
	out(" IISFIC=");
	hex(*IISFIC);
	out(" IISPSR=");
	hex(*IISPSR);
	out("\n  CLKSRC=");
	hex(clk[0x20 / 4]);
	out(" CLKDIV1=");
	hex(clk[0x28 / 4]);
	out(" PCLKCON=");
	hex(clk[0x34 / 4]);
	out(" SCLKCON=");
	hex(clk[0x38 / 4]);
	out("\n  GPBCON=");
	hex(gpio[GPBCON / 4]);
	out(" GPBDAT=");
	hex(gpio[GPBDAT / 4]);
	out(" GPECON=");
	hex(gpio[GPECON / 4]);
	out(" GPEDAT=");
	hex(gpio[GPEDAT / 4]);
	out("\n  GPGCON=");
	hex(gpio[GPGCON / 4]);
	out(" GPGDAT=");
	hex(gpio[GPGDAT / 4]);
	out(" GPJCON=");
	hex(gpio[GPJCON / 4]);
	out(" GPJDAT=");
	hex(gpio[GPJDAT / 4]);
	out("\n  AMP_ON(G11)=");
	dec((gpio[GPGDAT / 4] >> 11) & 1);
	out(" HPDET(G3)=");
	dec((gpio[GPGDAT / 4] >> 3) & 1);
	out(" DAC_PWR(J15)=");
	dec((gpio[GPJDAT / 4] >> 15) & 1);
	out(" TXCNT=");
	dec(TXCNT());
	out(" RXCNT=");
	dec(RXCNT());
	out("\n");
}

static void l3_send(u16 w)
{
	int i;

	gw(GPBDAT, 1u << 3, 0);
	for (i = 0; i < 16; i++) {
		gw(GPBDAT, 1u << 4, 0);
		gw(GPBDAT, 1u << 2, (w & 0x8000) ? 1u << 2 : 0);
		gw(GPBDAT, 1u << 4, 1u << 4);
		w <<= 1;
	}
	gw(GPBDAT, 1u << 3, 1u << 3);
}

static void codec(u16 reg, u16 val)
{
	l3_send((u16)((reg << 9) | (val & 0x1ff)));
}

static const u16 wm8971[43] = {
	0x80, 0x80, 0x79, 0x79, 0xffff, 0x08, 0xffff, 0x02, 0x20, 0xffff,
	0xff, 0xff, 0x0f, 0x0f, 0xffff, 0xffff, 0xffff, 0x7b, 0x00, 0x32,
	0x00, 0x1ff, 0x1ff, 0xc0, 0x10, 0xfe, 0x1e4, 0x00, 0xffff, 0xffff,
	0xffff, 0x00, 0x00, 0x00, 0x150, 0x50, 0x50, 0x150, 0x150, 0x150,
	0x79, 0x79, 0x80,
};

static int isin(u32 p)
{
	u32 x = p & 0x7fff;
	int y = (int)((x * (32768 - x)) >> 15);

	return (p & 0x8000) ? -y : y;
}

static void clocks_on(void)
{
	wr(clk + 0x34 / 4, 1u << 9, 1u << 9);
	wr(clk + 0x38 / 4, 1u << 9, 1u << 9);
	wr(clk + 0x20 / 4, 3u << 14, 2u << 14);
}

static void iis_setup(u32 txr)
{
	*IISCON = 0;
	*IISMOD = (1u << 7) | (txr << 8) | (1u << 10);
	*IISFIC = (1u << 15) | (1u << 7);
	*IISFIC = 0;
	*IISPSR = 1u << 15;
}

/* Time how long the TX FIFO needs to drain from `fill` words with IISCON = con. */
static void probe_one(u32 con, u32 txr)
{
	u32 i, t0, t1, c0, c1, guard = 0;

	iis_setup(txr);
	for (i = 0; i < 8; i++)
		*IISTXD = 0;
	c0 = TXCNT();
	out("probe IISCON=");
	dec(con);
	out(" TXR=");
	dec(txr);
	out(": 8 writes with IIS stopped -> TXCNT=");
	dec(c0);
	out(c0 ? " (CPU writes land)\n" : " (CPU writes DROPPED while stopped)\n");

	*IISCON = con;
	t0 = now_us();
	while (TXCNT() && guard < 2000000)
		guard++;
	t1 = now_us();
	c1 = TXCNT();
	out("  after start: TXCNT=");
	dec(c1);
	out(" drained in ");
	dec(t1 - t0);
	out(" us");
	if (!c1 && c0 && t1 > t0) {
		out(" -> ~");
		dec(udiv(c0 * 1000000u, t1 - t0));
		out(" frames/s (expect ~46875)");
	}
	out("\n");

	for (i = 0; i < 64; i++)
		*IISTXD = 0;
	out("  64 writes while running -> TXCNT=");
	dec(TXCNT());
	out(" TXFULL=");
	dec(TXFULL() ? 1 : 0);
	out("\n");
	msleep(100);
	out("  RXCNT after 100 ms=");
	dec(RXCNT());
	out(" (RX filling proves LRCK/BCLK are toggling when TXR includes RX)\n");
	dump("probe");
	*IISCON = 0;
}

static void power_up(void)
{
	gw(GPGDAT, 1u << 11, 0);
	gw(GPGCON, 0x00C00000, 0x00400000);
	gw(GPGCON, 0xC0, 0);
	gw(GPJDAT, 1u << 15, 1u << 15);
	gw(GPJCON, 0xC0000000, 0x40000000);
	msleep(50);
}

static void power_down(void)
{
	gw(GPGDAT, 1u << 11, 0);
	codec(5, 0x08);
	*IISCON = 0;
	gw(GPEDAT, 1u << 4, 0);
	gw(GPECON, 0x300, 0x100);
	gw(GPJDAT, 1u << 15, 0);
}

static void codec_init(u32 vol)
{
	u32 i;

	gw(GPBDAT, 0x1c, 0x18);
	gw(GPBCON, 0x3f0, 0x150);
	codec(15, 0);
	msleep(5);
	for (i = 0; i < 43; i++) {
		u16 r = wm8971[i];

		if (r == 0xffff)
			continue;
		if (i == 26)
			r |= 0x18;
		codec((u16)i, r);
	}
	codec(2, 0x80 | vol);
	codec(3, 0x180 | vol);
	codec(40, 0x80 | vol);
	codec(41, 0x180 | vol);
	codec(5, 0);
}

static void capture(u32 secs, u32 sel)
{
	u32 s, n, end, peakl = 0, peakr = 0, suml = 0, sumr = 0, cnt = 0;
	u32 lo = sel, hi = sel;

	if (sel > 3) {
		lo = 0;
		hi = 3;
	}
	for (s = lo; s <= hi; s++) {
		codec(0, 0x100 | mic_gain);
		codec(1, 0x100 | mic_gain);
		codec(32, (s << 6) | 0x20);
		codec(33, (s << 6) | 0x20);
		*IISFIC = 1u << 7;
		*IISFIC = 0;
		peakl = peakr = suml = sumr = cnt = 0;
		end = now_us() + secs * 1000000u;
		while ((int)(end - now_us()) > 0) {
			for (n = RXCNT(); n; n--) {
				u32 w = *IISRXD;
				int l = (s16)(w >> 16), r = (s16)(w & 0xffff);
				u32 al = (u32)(l < 0 ? -l : l), ar = (u32)(r < 0 ? -r : r);

				peakl = al > peakl ? al : peakl;
				peakr = ar > peakr ? ar : peakr;
				suml += al >> 4;
				sumr += ar >> 4;
				if (dump_raw && cnt < 49152)
					rawbuf[cnt] = w;
				cnt++;
			}
			if (!TXFULL())
				*IISTXD = 0;
		}
		if (dump_raw) {
			u32 nw = cnt < 49152 ? cnt : 49152;

			sc(4, 1, (long)rawbuf, nw * 4, 0, 0, 0);
			continue;
		}
		out("mic insel=");
		dec(s);
		out(" frames=");
		dec(cnt);
		out(" peakL=");
		dec(peakl);
		out(" peakR=");
		dec(peakr);
		out(" meanL=");
		dec(udiv(suml, cnt) << 4);
		out(" meanR=");
		dec(udiv(sumr, cnt) << 4);
		out("\n");
	}
}

static void sdec(int v)
{
	if (v < 0) {
		out("-");
		v = -v;
	}
	dec((u32)v);
}

static s16 mbuf[16384];

/* S seconds of 250 ms windows on the right channel; one "m level peak left" line each, written at once (no stdio buffer) */
static void micmeter(u32 secs, u32 sel)
{
	u32 end, wend, n, cnt;
	int lv, pk;

	if (sel > 3)
		sel = 1;
	codec(0, 0x100 | mic_gain);
	codec(1, 0x100 | mic_gain);
	codec(32, (sel << 6) | 0x20);
	codec(33, (sel << 6) | 0x20);
	*IISFIC = 1u << 7;
	*IISFIC = 0;
	end = now_us() + secs * 1000000u;
	while ((int)(end - now_us()) > 0) {
		wend = now_us() + 250000u;
		cnt = 0;
		while ((int)(wend - now_us()) > 0) {
			for (n = RXCNT(); n; n--) {
				u32 w = *IISRXD;

				if (cnt < 16384)
					mbuf[cnt++] = (s16)(w & 0xffff);
			}
			if (!TXFULL())
				*IISTXD = 0;
		}
		if (!cnt)
			continue;
		mm_window(mbuf, cnt, &lv, &pk);
		out("m ");
		sdec(lv);
		out(" ");
		sdec(pk);
		out(" ");
		n = (u32)(end - now_us());
		dec((int)n > 0 ? udiv(n + 999999u, 1000000u) : 0);
		out("\n");
	}
}

int main_c(long *sp)
{
	int argc = (int)sp[0];
	char **argv = (char **)(sp + 1);
	u32 hz = 440, secs = 2, vol = 117, hold = 0, con = 1, txr = 0, probe = 0, mic = 0, micsel = 4, amp = 1, meter = 0, mm_set = 0;
	u32 n, i, step, ph = 0, total, stalls = 0, t0, t1, maxcnt = 0, hp;
	long fd;

	for (i = 1; i < (u32)argc; i++) {
		const char *a = argv[i], *v = (i + 1 < (u32)argc) ? argv[i + 1] : 0;

		if (eq(a, "--hz"))
			hz = num(v), i++;
		else if (eq(a, "--secs"))
			secs = num(v), i++;
		else if (eq(a, "--vol"))
			vol = num(v), i++;
		else if (eq(a, "--hold"))
			hold = num(v), i++;
		else if (eq(a, "--iiscon"))
			con = num(v), i++;
		else if (eq(a, "--mic"))
			mic = num(v), i++;
		else if (eq(a, "--micmeter"))
			meter = num(v), mic = 1, mm_set = 1, i++;
		else if (eq(a, "--gain"))
			mic_gain = num(v) & 0x3f, i++;
		else if (eq(a, "--micdump"))
			dump_raw = 1;
		else if (eq(a, "--micsel"))
			micsel = num(v), i++;
		else if (eq(a, "--amp")) {
			amp = (v && eq(v, "off")) ? 0 : (v && eq(v, "auto")) ? 2 : 1;
			i++;
		} else if (eq(a, "--xr"))
			txr = 2;
		else if (eq(a, "--probe"))
			probe = 1;
		else {
			out("unknown argument\n");
			return 2;
		}
	}
	if (hz < 50 || hz > 10000 || secs > 30 || hold > 60 || mic > 10 || meter > 60 || (mic && !meter && mm_set) || vol > 127 || (con != 1 && con != 7))
		return 2;
	if (vol < 48)
		out("warning: --vol below 48 is muted on WM8750\n");

	{
		long prio = 60;

		/* the TX FIFO holds 16 frames (0.3 ms): feed it at real-time priority or every other process makes it underrun */
		if (!mic)
			sc(156, 0, 1, (long)&prio, 0, 0, 0);	/* sched_setscheduler(0, SCHED_FIFO, 60); capture keeps normal priority so the UI stays responsive */
	}
	fd = sc(5, (long)"/dev/mem", 2 | 0x101000, 0, 0, 0, 0);
	if (fd < 0)
		return 1;
	clk = map(fd, 0x4C000000);
	iis = map(fd, 0x55000000);
	gpio = map(fd, 0x56000000);
	if (!clk || !iis || !gpio)
		return 1;

	dump("before");
	clocks_on();
	gw(GPECON, 0x3ff, 0x2aa);

	if (probe) {
		probe_one(1, 0);
		probe_one(7, 0);
		probe_one(1, 2);
		probe_one(7, 2);
		return 0;
	}

	power_up();
	hp = !((gpio[GPGDAT / 4] >> 3) & 1);
	out("headphone detect after DAC power-up: ");
	out(hp ? "plugged (GPG3 low)\n" : "not plugged (GPG3 high)\n");

	iis_setup(mic ? 2 : txr);
	for (i = 0; i < 16; i++)
		*IISTXD = 0;
	*IISCON = con;
	msleep(20);
	codec_init(vol);
	dump("codec-ready");

	if (mic) {
		if (meter)
			micmeter(meter, micsel);
		else
			capture(mic, micsel);
		power_down();
		dump("after");
		return 0;
	}

	if (amp == 1 || (amp == 2 && !hp))
		gw(GPGDAT, 1u << 11, 1u << 11);

	total = 46875 * secs;
	step = (hz * 65536u) / 46875u;
	t0 = now_us();
	for (n = 0; n < total; n++) {
		u32 s = (u32)(s16)isin(ph) & 0xffff, c;

		if (TXFULL()) {
			stalls++;
			while (TXFULL())
				;
		}
		*IISTXD = (s << 16) | s;
		ph = (ph + step) & 0xffff;
		if (!(n & 1023)) {
			c = TXCNT();
			maxcnt = c > maxcnt ? c : maxcnt;
		}
		if (n == total / 2)
			dump("mid-play");
	}
	t1 = now_us();
	out("frames=");
	dec(total);
	out(" elapsed_us=");
	dec(t1 - t0);
	out(" rate=");
	dec(udiv(total * 100u, (t1 - t0) / 10000u));
	out(" fifo_full_stalls=");
	dec(stalls);
	out(" max_txcnt=");
	dec(maxcnt);
	out(stalls ? "\n" : " -> FIFO NEVER FILLED: writes are dropped or the frame clock is far too fast\n");

	if (hold) {
		out("holding ");
		dec(hold);
		out(" s\n");
		msleep(hold * 1000);
		dump("hold-end");
	}
	msleep(20);
	power_down();
	dump("after");
	return 0;
}

__asm__(".globl _start\n_start:\n mov r0, sp\n bl main_c\n mov r7, #1\n svc 0\n");
