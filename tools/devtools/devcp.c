/* devcp FILE PHYSADDR: copy a file into physical memory through /dev/mem (e.g. a raw RGB565 480x272 frame to the LCD
 * framebuffer at 0x33F00000). Freestanding ARMv4T (raw EABI syscalls). */
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

int main_c(long *sp)
{
	int argc = (int)sp[0];
	char **argv = (char **)(sp + 1);
	long in, mem, m, n, off = 0;
	u32 a;

	if (argc < 3)
		return 2;
	a = num(argv[2]);
	in = sc(5, (long)argv[1], 0, 0, 0, 0, 0);
	mem = sc(5, (long)"/dev/mem", 2 | 0x101000, 0, 0, 0, 0);
	if (in < 0 || mem < 0)
		return 1;
	m = sc(192, 0, 0x100000, 3, 1, mem, a >> 12);	/* mmap2, up to 1 MiB window */
	if (m < 0 && m > -4096)
		return 1;
	while ((n = sc(3, in, m + off, 0x100000 - off, 0, 0, 0)) > 0)
		off += n;
	return off > 0 ? 0 : 1;
}

__asm__(".globl _start\n_start:\n mov r0, sp\n bl main_c\n mov r7, #1\n svc 0\n");
