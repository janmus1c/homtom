/* hcictl up | cmd B0 B1 ...  : minimal HCI client on hci0 (raw socket), freestanding ARMv4T.
 *   up            bring hci0 up (the kernel runs its init: reset, read features...) 
 *   cmd 03 0c 00  send an HCI command (opcode LE bytes + length + params), print every event received for ~2 s as hex */
typedef unsigned int u32;
typedef unsigned char u8;
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

static int hex(const char *s)
{
	int v = 0;

	for (; *s; s++)
		v = v * 16 + (*s >= 'a' ? *s - 'a' + 10 : *s >= 'A' ? *s - 'A' + 10 : *s - '0');
	return v;
}

static void out(const u8 *b, int n)
{
	char t[3 * 260 + 1];
	int i, k = 0;

	for (i = 0; i < n && k < 3 * 256; i++) {
		t[k++] = "0123456789abcdef"[b[i] >> 4];
		t[k++] = "0123456789abcdef"[b[i] & 15];
		t[k++] = ' ';
	}
	t[k++] = '\n';
	sc(4, 1, (long)t, k, 0, 0, 0);
}

int main_c(long *sp)
{
	int argc = (int)sp[0];
	char **argv = (char **)(sp + 1);
	struct { u16 family, dev, channel; } addr = { 31, 0, 0 };
	u32 filt[4] = { 0xffffffff, 0xffffffff, 0xffffffff, 0 };	/* all packet types and events */
	long s;

	if (argc < 2)
		return 2;
	s = sc(281, 31, 3, 1, 0, 0, 0);		/* socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI) */
	if (s < 0)
		return 1;
	if (argv[1][0] == 'u') {
		long r = sc(54, s, 0x400448c9, 0, 0, 0, 0);	/* HCIDEVUP hci0 */

		return r < 0 && r != -17 ? 3 : 0;
	}
	sc(294, s, 0, 2, (long)filt, 16, 0);		/* setsockopt(SOL_HCI, HCI_FILTER) */
	if (sc(282, s, (long)&addr, 6, 0, 0, 0) < 0)
		return 4;
	{
		u8 pkt[260];
		int n = 1, i;

		pkt[0] = 1;
		for (i = 2; i < argc && n < 259; i++)
			pkt[n++] = hex(argv[i]);
		sc(290, s, (long)pkt, n, 0, 0, 0);
	}
	for (;;) {
		struct { int fd; short ev, rev; } p = { (int)s, 1, 0 };
		u8 b[260];
		long n;

		if (sc(168, (long)&p, 1, 2000, 0, 0, 0) <= 0)
			break;
		n = sc(291, s, (long)b, 260, 0, 0, 0);
		if (n <= 0)
			break;
		out(b, n);
	}
	return 0;
}

__asm__(".globl _start\n_start:\n mov r0, sp\n bl main_c\n mov r7, #1\n svc 0\n");
