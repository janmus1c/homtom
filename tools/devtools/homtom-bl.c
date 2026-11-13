/* homtom-bl LEVEL [PCLK_HZ [FREQ_HZ]]: backlight brightness 0-255 through S3C2443 timer 0 (TOUT0 on GPB0), via /dev/mem.
 * Freestanding (raw EABI syscalls, no libc), ARMv4T. Style and syscall helpers follow tools/devtools/devmem.c.
 *
 * Registers touched (all masked read-modify-write; nothing else is written):
 *   PCLKCON  0x4C000034 bit 10 (PWM gate)                         [V] L/drivers/clk/samsung/clk-s3c2443.c:133
 *   TCFG0    0x51000000 bits 7:0  (prescaler0; bits 15:8 = clocksource, left alone)   [V] HARDWARE_MAP.md:39
 *   TCFG1    0x51000004 bits 3:0  (timer 0 mux)
 *   TCON     0x51000008 bits 3:0  (timer 0 start, manual update, inverter, auto reload; bits 16-22 = clocksource, left alone)
 *   TCNTB0   0x5100000C, TCMPB0 0x51000010
 *   GPBCON   0x56000010 bits 1:0 (GPB0 only; GPB1 BT_MODE and GPB2-4 codec 3-wire bus left alone)
 *   GPBDAT   0x56000014 bit 0
 * No GPJ or GPL register is written. Not inverted: high = brighter [V] s3c_bl.c:106-137, 196-212.
 *
 * Scaling (stock s3c_bl.c:174-225, s3c_hw_init): stock level 0-100, range 160 on Modena/Sharp panel, tcmp = level * tcnt / 160.
 * So the stock maximum duty is 100/160 = 62.5 percent. This tool keeps that cap: 255 -> level 100 -> 62.5 percent duty [V].
 * Stock divider: prescaler 0 (SYS_TIMER01_PRESCALER), mux 1/2 (SYS_TIMER0_MUX), tcnt = PCLK / (2 * freq) - 1 [V] timer.h:21-24, s3c_bl.c:307-309.
 * Default freq 1000 Hz (Sharp), use 1839 for the Samsung LTE430WQ panel [V] HARDWARE_MAP.md 3.6.
 * PCLK default 66500000 is a hypothesis [H]; wrong PCLK only changes the PWM frequency (flicker/whine), not the duty fraction.
 * Do not run while pwm_backlight/pwm-samsung owns timer 0 (the kernel would fight for TCON).
 *
 * Build: clang --target=armv4t-linux-gnueabi -march=armv4t -mfloat-abi=soft -Os -nostdlib -static -fuse-ld=lld homtom-bl.c -o homtom-bl
 *   (or the arm gcc cross used for devmem; same flags as tools/devtools/devmem). */
typedef unsigned int u32;

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

static u32 num(const char *s)
{
	u32 v = 0;
	int base = 10;

	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		base = 16;
		s += 2;
	}
	for (; *s; s++) {
		int d = *s >= 'a' ? *s - 'a' + 10 : *s >= 'A' ? *s - 'A' + 10 : *s - '0';

		v = v * base + d;
	}
	return v;
}

static void msg(const char *s)
{
	int n = 0;

	while (s[n])
		n++;
	sc(4, 2, (long)s, n, 0, 0, 0);
}

static volatile u32 *map(long fd, u32 pa)
{
	long m = sc(192, 0, 4096, 3, 1, fd, pa >> 12);	/* mmap2 */

	return (m < 0 && m > -4096) ? 0 : (volatile u32 *)m;
}

static void wr(volatile u32 *p, u32 mask, u32 val)
{
	*p = (*p & ~mask) | (val & mask);
}

int main_c(long *sp)
{
	int argc = (int)sp[0];
	char **argv = (char **)(sp + 1);
	volatile u32 *clk, *tim, *gpio;
	u32 lvl, pclk = 66500000, freq = 1000, tcnt, tcmp, l100;
	long fd;

	if (argc < 2) {
		msg("usage: homtom-bl LEVEL(0-255) [PCLK_HZ [FREQ_HZ]]\n");
		return 2;
	}
	lvl = num(argv[1]);
	if (lvl > 255)
		lvl = 255;
	if (argc > 2)
		pclk = num(argv[2]);
	if (argc > 3)
		freq = num(argv[3]);
	if (!freq || pclk / (2 * freq) < 2 || pclk / (2 * freq) > 65536) {
		msg("bad PCLK/FREQ\n");
		return 2;
	}
	tcnt = pclk / (2 * freq) - 1;

	fd = sc(5, (long)"/dev/mem", 2 | 0x101000, 0, 0, 0, 0);	/* O_RDWR|O_SYNC */
	if (fd < 0)
		return 1;
	clk = map(fd, 0x4C000000);
	tim = map(fd, 0x51000000);
	gpio = map(fd, 0x56000000);
	if (!clk || !tim || !gpio)
		return 1;

	l100 = (lvl * 100 + 127) / 255;			/* stock level 0-100, capped at 62.5 percent duty */
	tcmp = (l100 * tcnt + 80) / 160;		/* divrnd(level * tcnt, range 160) */

	wr(clk + 0x34 / 4, 1u << 10, 1u << 10);		/* PCLKCON PWM gate on */

	if (l100 == 0) {
		wr(tim + 0x08 / 4, 0xf, 0);		/* timer 0 stop */
		wr(gpio + 0x14 / 4, 1, 0);		/* GPBDAT0 low */
		wr(gpio + 0x10 / 4, 3, 1);		/* GPBCON0 output */
		return 0;
	}

	if ((*(tim + 0x08 / 4) & 1) && (*(gpio + 0x10 / 4) & 3) == 2 && *(tim + 0x0c / 4) == tcnt) {
		*(tim + 0x10 / 4) = tcmp;		/* running: auto-reload picks up TCMPB0 */
		return 0;
	}

	wr(tim + 0x00 / 4, 0xff, 0);			/* prescaler0 = 0 (divide by 1) */
	wr(tim + 0x04 / 4, 0xf, 0);			/* timer 0 mux 1/2 */
	*(tim + 0x0c / 4) = tcnt;
	*(tim + 0x10 / 4) = tcmp;
	wr(tim + 0x08 / 4, 0xf, 0x2 | 0x8);		/* manual update + auto reload, not inverted */
	wr(tim + 0x08 / 4, 0xf, 0x1 | 0x8);		/* start + auto reload */
	wr(gpio + 0x10 / 4, 3, 2);			/* GPB0 = TOUT0 (function 2) */
	return 0;
}

__asm__(".globl _start\n_start:\n mov r0, sp\n bl main_c\n mov r7, #1\n svc 0\n");

/* No libgcc: shift-and-subtract unsigned division for the compiler's division calls. */
unsigned int __aeabi_uidiv(unsigned int n, unsigned int d)
{
	unsigned int q = 0, r = 0;
	int i;

	if (!d)
		return 0;
	for (i = 31; i >= 0; i--) {
		r = (r << 1) | ((n >> i) & 1);
		if (r >= d) {
			r -= d;
			q |= 1u << i;
		}
	}
	return q;
}
