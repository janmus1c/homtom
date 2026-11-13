/* hciattach TTY PROTO: attach a UART to the kernel HCI line discipline (N_HCI=15) and keep it open.
 * PROTO: 0 H4, 1 BCSP, 2 H5/3wire, 3 ... (HCI_UART_* numbers from the hci_uart driver). Set baud/parity with stty first.
 * Freestanding ARMv4T (raw EABI syscalls). */
static long sc(long n, long a, long b, long c)
{
	register long r7 __asm__("r7") = n;
	register long r0 __asm__("r0") = a;
	register long r1 __asm__("r1") = b;
	register long r2 __asm__("r2") = c;

	__asm__ volatile("svc 0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2) : "memory");
	return r0;
}

int main_c(long *sp)
{
	int argc = (int)sp[0];
	char **argv = (char **)(sp + 1);
	long fd;
	int ldisc = 15, proto;

	if (argc < 3)
		return 2;
	proto = argv[2][0] - '0';
	fd = sc(5, (long)argv[1], 2 | 0x100, 0);		/* O_RDWR | O_NOCTTY */
	if (fd < 0)
		return 1;
	if (sc(54, fd, 0x5423, (long)&ldisc) < 0)		/* TIOCSETD */
		return 3;
	if (sc(54, fd, 0x400455c8, proto) < 0)			/* HCIUARTSETPROTO */
		return 4;
	for (;;)
		sc(29, 0, 0, 0);				/* pause() */
}

__asm__(".globl _start\n_start:\n mov r0, sp\n bl main_c\n mov r7, #1\n svc 0\n");
