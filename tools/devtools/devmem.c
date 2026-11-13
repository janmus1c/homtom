/* devmem ADDR [VALUE [MASK]]: read or write one 32-bit physical register via /dev/mem.
 * Freestanding (raw EABI syscalls, no libc): the target is ARMv4T and Debian's glibc is ARMv5TE. */
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

static void out(u32 a, u32 v)
{
	char b[20];
	int i;

	for (i = 0; i < 8; i++)
		b[i] = "0123456789abcdef"[(a >> (28 - 4 * i)) & 15];
	b[8] = ':';
	b[9] = ' ';
	for (i = 0; i < 8; i++)
		b[10 + i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15];
	b[18] = '\n';
	sc(4, 1, (long)b, 19, 0, 0, 0);
}

int main_c(long *sp)
{
	int argc = (int)sp[0];
	char **argv = (char **)(sp + 1);
	u32 a, pg, v;
	volatile u32 *p;
	long fd, m;

	if (argc < 2)
		return 2;
	a = num(argv[1]);
	pg = a & ~0xfffu;
	fd = sc(5, (long)"/dev/mem", 2 | 0x101000, 0, 0, 0, 0);	/* O_RDWR|O_SYNC */
	if (fd < 0)
		return 1;
	m = sc(192, 0, 4096, 3, 1, fd, pg >> 12);	/* mmap2 */
	if (m < 0 && m > -4096)
		return 1;
	p = (volatile u32 *)(m + (a - pg));
	if (argc > 2) {
		u32 mk = 0xffffffffu;

		v = num(argv[2]);
		if (argc > 3)
			mk = num(argv[3]);
		/* Hazards (docs/HARDWARE_MAP.md): GPJDAT bit 10 = PWR_RST (high = power off), bit 14 = moviNAND power; GPJCON pins 10/14;
		 * GPLCON pins 0-9 = moviNAND bus. Writes must carry a mask that leaves them alone. */
		if ((a == 0x560000D4 && (mk & 0x4400)) || (a == 0x560000D0 && (mk & 0x30300000)) ||
		    (a == 0x560000F0 && (mk & 0x000fffff)))
			return 5;
		v = (*p & ~mk) | (v & mk);
		*p = v;
	}
	out(a, *p);
	return 0;
}

__asm__(".globl _start\n_start:\n mov r0, sp\n bl main_c\n mov r7, #1\n svc 0\n");
