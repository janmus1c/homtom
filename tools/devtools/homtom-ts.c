/* Resistive touch sampler for the GO 630 (stock: tomtomgo adc.c). The panel is switched by external FETs on GPG12-15
 * (XPON/YPON high = on, XMON/YMON low = on) and read through ADC mux 5 (X) and 9 (Y); pen-down = mux 9 with the ADC pull-up.
 * Prints "x y" (10-bit raw) per sample while the pen is down, and "up" once on release. Freestanding ARMv4T, /dev/mem.
 * usage: homtom-ts [samples-per-second-limit-ignored]; run until killed. */
typedef unsigned int u32;
typedef unsigned char u8;

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

#define GPG_CON	0x60
#define GPG_DAT	0x64
#define ADCCON	0x00
#define ADCTSC	0x04
#define ADCDAT0	0x0c
#define ADCMUX	0x18

static volatile u32 *gpio, *adc;

static void msleep(long ms)
{
	long ts[2] = { 0, ms * 1000000 };

	sc(162, (long)ts, 0, 0, 0, 0, 0);
}

static void gpg(u32 xpon, u32 xmon, u32 ypon, u32 ymon)
{
	u32 v = gpio[GPG_DAT / 4] & ~0xf000u;

	v |= (xpon << 12) | (xmon << 13) | (ypon << 14) | (ymon << 15);
	gpio[GPG_DAT / 4] = v;
}

static u32 sample(int mux)
{
	int i;

	adc[ADCMUX / 4] = mux;
	adc[ADCCON / 4] = (1u << 14) | (49u << 6) | 1u;
	for (i = 0; i < 400; i++)
		__asm__ volatile("" ::: "memory");	/* let ECFLG clear before polling it */
	for (i = 0; i < 100000 && !(adc[ADCCON / 4] & 0x8000); i++)
		;
	return adc[ADCDAT0 / 4] & 0x3ff;
}

static void put(u32 x, u32 y, int up)
{
	char b[32];
	int n = 0, i;
	u32 v[2] = { x, y };

	if (up) {
		sc(4, 1, (long)"up\n", 3, 0, 0, 0);
		return;
	}
	for (i = 0; i < 2; i++) {
		char t[6];
		int k = 0;

		{
			u32 q = 0, r = v[i];

			do {			/* no libgcc: divide by 10 with a plain loop (values <= 1023) */
				q = 0;
				while (r >= 10) {
					r -= 10;
					q++;
				}
				t[k++] = '0' + r;
				r = q;
			} while (r);
		}
		while (k)
			b[n++] = t[--k];
		b[n++] = i ? '\n' : ' ';
	}
	sc(4, 1, (long)b, n, 0, 0, 0);
}


static int pn(char *b, u32 r)
{
	char t[6];
	int k = 0, n = 0;
	u32 q;

	do {
		q = 0;
		while (r >= 10) {
			r -= 10;
			q++;
		}
		t[k++] = '0' + r;
		r = q;
	} while (r);
	while (k)
		b[n++] = t[--k];
	return n;
}

/* debug: for each ADCTSC mode print raw mux 9/5/7 with the panel planes off, X-driven and Y-driven (run with any argument) */
static void debug(void)
{
	static const u32 modes[3] = { 0x00, 0x50, 0x58 };
	int m, p;

	for (;;) {
		for (m = 0; m < 3; m++) {
			char b[96];
			int n = 0;
			u32 v;

			adc[ADCTSC / 4] = modes[m];
			for (p = 0; p < 3; p++) {
				if (p == 0)
					gpg(0, 1, 0, 0);
				else if (p == 1)
					gpg(1, 0, 0, 1);
				else
					gpg(0, 1, 1, 0);
				msleep(3);
				n += pn(b + n, modes[m]);
				b[n++] = ':';
				v = sample(9);
				n += pn(b + n, v);
				b[n++] = '/';
				v = sample(5);
				n += pn(b + n, v);
				b[n++] = '/';
				v = sample(7);
				n += pn(b + n, v);
				b[n++] = ' ';
			}
			b[n++] = '\n';
			sc(4, 1, (long)b, n, 0, 0, 0);
		}
		sc(4, 1, (long)"--\n", 3, 0, 0, 0);
		msleep(300);
	}
}

/* k mode: log to /dev/kmsg (shown on the LCD console) when a reading moves by more than 6 or every ~5 s: "ts off x y" = mux9/5/7 per plane */
static void kmode(void)
{
	long fd = sc(5, (long)"/dev/kmsg", 1, 0, 0, 0, 0);
	static u32 last[9];
	u32 cur[9];
	int p, m, i, tick = 0, changed;
	static const u32 mux[3] = { 9, 5, 7 };

	if (fd < 0)
		return;
	adc[ADCTSC / 4] = 0x50;
	for (;;) {
		changed = 0;
		for (p = 0; p < 3; p++) {
			if (p == 0)
				gpg(0, 1, 0, 0);
			else if (p == 1)
				gpg(1, 0, 0, 1);
			else
				gpg(0, 1, 1, 0);
			msleep(3);
			for (m = 0; m < 3; m++) {
				cur[p * 3 + m] = sample(mux[m]);
				i = p * 3 + m;
				if (cur[i] + 6 < last[i] || cur[i] > last[i] + 6)
					changed = 1;
			}
		}
		if (changed || ++tick >= 25) {
			char b[96];
			int n = 3;

			b[0] = 't';
			b[1] = 's';
			b[2] = ' ';

			for (i = 0; i < 9; i++) {
				n += pn(b + n, cur[i]);
				b[n++] = (i % 3 == 2) ? ' ' : '/';
				last[i] = cur[i];
			}
			b[n++] = '\n';
			sc(4, fd, (long)b, n, 0, 0, 0);
			tick = 0;
		}
		msleep(200);
	}
}

int main_c(long *sp)
{
	long fd = sc(5, (long)"/dev/mem", 2 | 0x101000, 0, 0, 0, 0);
	int down = 0;
	long g, a;

	if (fd < 0)
		return 1;
	g = sc(192, 0, 4096, 3, 1, fd, 0x56000);
	a = sc(192, 0, 4096, 3, 1, fd, 0x58000);
	if ((g < 0 && g > -4096) || (a < 0 && a > -4096))
		return 1;
	gpio = (volatile u32 *)g;
	adc = (volatile u32 *)a;
	/* GPG12-15 outputs */
	gpio[GPG_CON / 4] = (gpio[GPG_CON / 4] & 0x00ffffffu) | 0x55000000u;
	if (sp[0] > 1) {
		char *a1 = *(char **)(sp + 2);

		if (a1[0] == 'k')
			kmode();
		debug();
	}
	for (;;) {
		u32 x, y, p;

		adc[ADCTSC / 4] = 0;		/* internal switches off, pull-up enabled */
		gpg(0, 1, 0, 0);		/* FETs off, TSDOWN (GPG15) low */
		msleep(2);
		p = sample(9);
		if (p > 900) {
			if (down)
				put(0, 0, 1);
			down = 0;
			msleep(20);
			continue;
		}
		gpg(1, 0, 0, 1);		/* X plane: XMON low, XPON high */
		msleep(2);
		x = sample(5);
		gpg(0, 1, 1, 0);		/* Y plane: YMON low, YPON high */
		msleep(2);
		y = sample(9);
		down = 1;
		put(x, y, 0);
		msleep(10);
	}
}

__asm__(".globl _start\n_start:\n mov r0, sp\n bl main_c\n mov r7, #1\n svc 0\n");
