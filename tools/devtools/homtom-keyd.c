/* homtom-keyd: power-key daemon for the TomTom GO 630.
 *
 * Reads KEY_POWER (116) from the evdev device "homtom-keys" (kernel k3, patch 0005) and turns it into:
 *   short press            "key short\n" into the GUI's injection pipe /tmp/ui.in
 *   hold 1.5 s .. 5 s      "key long\n" there, when the key is released
 *   hold reaches 5 s       "key hold5\n" (UI hint: release to power off)
 *   release at 5 s .. 10 s /bin/homtom-poweroff (INFORM0=1 + SWRST: TTBL "Power down"). It runs on the release because
 *                          TTBL boots on when it finds the key held.
 *   hold reaches 10 s      "key hold10\n", then /bin/reboot (devmem wrapper: INFORM0=5 + SWRST, storage window follows)
 * The timing rules live in homtom-keyd-logic.h (host-tested). Decisions are logged to /dev/kmsg as "homtom-keyd: ...".
 *
 * The device is found by name in /proc/bus/input/devices (fallback /dev/input/event0). A key found down at start is
 * ignored until released. If /tmp/ui.in is missing, not a FIFO, or has no reader, the line is dropped silently.
 * Exit status 1 when the input device is lost; homtom-keyd-start respawns.
 *
 * Freestanding ARMv4T EABI, raw syscalls, no libc, no libgcc. Build: tools/devtools/build.sh homtom-keyd
 */
#include "homtom-keyd-logic.h"

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

#define SYS_EXIT	1
#define SYS_FORK	2
#define SYS_READ	3
#define SYS_WRITE	4
#define SYS_OPEN	5
#define SYS_CLOSE	6
#define SYS_EXECVE	11
#define SYS_IOCTL	54
#define SYS_POLL	168
#define SYS_RT_SIGACTION 174
#define SYS_FSTAT64	197
#define SYS_CLOCK_GETTIME 263

#define O_RDONLY	0
#define O_WRONLY	1
#define O_NONBLOCK	04000
#define O_CLOEXEC	02000000
#define EINTR		4
#define POLLIN		1
#define EV_KEY		1
#define KEY_POWER	116
#define EVIOCGKEY16	0x80104518u	/* _IOR('E', 0x18, 16 bytes): 128 key bits, enough for code 116 */
#define CLOCK_MONOTONIC	1
#define SIG_DFL		0
#define SIG_IGN		1

struct pollfd {
	int fd;
	short events;
	short revents;
};

struct input_event {
	u32 sec;
	u32 usec;
	unsigned short type;
	unsigned short code;
	int value;
};

static int slen(const char *s)
{
	int n = 0;

	while (s[n])
		n++;
	return n;
}

/* u32 / 1000000 without a divide instruction */
static u32 div_1m(u32 n)
{
	u32 q = 0, d = 1000000u << 12, b = 1u << 12;

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

static u32 now_ms(void)
{
	long ts[2];	/* 32-bit timespec: seconds, nanoseconds */

	if (sc(SYS_CLOCK_GETTIME, CLOCK_MONOTONIC, (long)ts, 0, 0, 0, 0) < 0)
		return 0;
	return (u32)ts[0] * 1000u + div_1m((u32)ts[1]);
}

static void klog(const char *a, const char *b)
{
	char buf[160];
	const char *p = "homtom-keyd: ";
	int n = 0, i;
	long fd;

	for (i = 0; p[i] && n < 150; i++)
		buf[n++] = p[i];
	for (i = 0; a && a[i] && n < 150; i++)
		buf[n++] = a[i];
	for (i = 0; b && b[i] && n < 150; i++)
		buf[n++] = b[i];
	buf[n++] = '\n';
	fd = sc(SYS_OPEN, (long)"/dev/kmsg", O_WRONLY | O_NONBLOCK, 0, 0, 0, 0);
	if (fd < 0)
		return;
	sc(SYS_WRITE, fd, (long)buf, n, 0, 0, 0);
	sc(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
}

static void send_ui(const char *line)
{
	unsigned int st[24];	/* struct stat64 is 104 bytes; st_mode sits at byte 16 */
	long fd = sc(SYS_OPEN, (long)"/tmp/ui.in", O_WRONLY | O_NONBLOCK | O_CLOEXEC, 0, 0, 0, 0);

	if (fd < 0) {
		klog("no GUI pipe, dropped ", line);
		return;
	}
	if (sc(SYS_FSTAT64, fd, (long)st, 0, 0, 0, 0) == 0 && (st[4] & 0170000) == 0010000)
		sc(SYS_WRITE, fd, (long)line, slen(line), 0, 0, 0);
	sc(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
}

static void run(const char *path)
{
	static char *const envp[] = { "PATH=/bin:/sbin:/usr/bin:/usr/sbin", 0 };
	char *argv[2];
	long pid = sc(SYS_FORK, 0, 0, 0, 0, 0, 0);

	if (pid != 0) {
		if (pid < 0)
			klog("fork failed for ", path);
		return;
	}
	{
		struct {
			long handler;
			unsigned long flags;
			long restorer;
			unsigned long mask[2];
		} act = { SIG_DFL, 0, 0, { 0, 0 } };

		sc(SYS_RT_SIGACTION, 13, (long)&act, 0, 8, 0, 0);
		sc(SYS_RT_SIGACTION, 17, (long)&act, 0, 8, 0, 0);
	}
	argv[0] = (char *)path;
	argv[1] = 0;
	sc(SYS_EXECVE, (long)path, (long)argv, (long)envp, 0, 0, 0);
	sc(SYS_EXIT, 127, 0, 0, 0, 0, 0);
}

static void ignore_signals(void)
{
	struct {
		long handler;
		unsigned long flags;
		long restorer;
		unsigned long mask[2];
	} act = { SIG_IGN, 0, 0, { 0, 0 } };

	sc(SYS_RT_SIGACTION, 13, (long)&act, 0, 8, 0, 0);	/* SIGPIPE: a pipe whose reader just left */
	sc(SYS_RT_SIGACTION, 17, (long)&act, 0, 8, 0, 0);	/* SIGCHLD: children are reaped by the kernel */
}

static int find_event_number(void)
{
	static char buf[8192];
	long fd = sc(SYS_OPEN, (long)"/proc/bus/input/devices", O_RDONLY, 0, 0, 0, 0);
	long n, i, j;
	static const char name[] = "Name=\"homtom-keys\"";
	int nl = sizeof(name) - 1;

	if (fd < 0)
		return 0;
	n = sc(SYS_READ, fd, (long)buf, sizeof(buf) - 1, 0, 0, 0);
	sc(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
	if (n <= 0)
		return 0;
	buf[n] = 0;
	for (i = 0; i + nl < n; i++) {
		for (j = 0; j < nl && buf[i + j] == name[j]; j++)
			;
		if (j < nl)
			continue;
		for (i += nl; i < n && !(buf[i] == '\n' && (i + 1 >= n || buf[i + 1] == '\n')); i++) {
			if (buf[i] == 'e' && i + 6 < n && buf[i + 1] == 'v' && buf[i + 2] == 'e' && buf[i + 3] == 'n' &&
			    buf[i + 4] == 't' && buf[i + 5] >= '0' && buf[i + 5] <= '9') {
				int v = 0;

				for (j = i + 5; j < n && buf[j] >= '0' && buf[j] <= '9' && v < 1000; j++)
					v = v * 10 + (buf[j] - '0');
				return v;
			}
		}
		return 0;
	}
	return 0;
}

static int key_is_down(long fd)
{
	unsigned char bits[16];
	int i;

	for (i = 0; i < 16; i++)
		bits[i] = 0;
	if (sc(SYS_IOCTL, fd, EVIOCGKEY16, (long)bits, 0, 0, 0) < 0)
		return 0;
	return (bits[KEY_POWER >> 3] >> (KEY_POWER & 7)) & 1;
}

static void act(enum kd_action a)
{
	switch (a) {
	case KD_SHORT:
		klog("short press", 0);
		send_ui("key short\n");
		break;
	case KD_LONG:
		klog("long press", 0);
		send_ui("key long\n");
		break;
	case KD_HOLD5:
		klog("hold 5 s: release to power off", 0);
		send_ui("key hold5\n");
		break;
	case KD_REBOOT:
		klog("hold 10 s: emergency reboot to the storage window", 0);
		send_ui("key hold10\n");
		run("/bin/reboot");
		break;
	case KD_POWEROFF:
		klog("released after 5 s: power off", 0);
		run("/bin/homtom-poweroff");
		break;
	default:
		break;
	}
}

int main(void)
{
	static char path[32] = "/dev/input/event";
	static struct input_event ev[8];
	struct pollfd pfd;
	struct kd k;
	int num = find_event_number(), p = 16, d;
	long fd, n;

	for (d = 100; d >= 1; d /= 10) {	/* constant divisors compile to multiplies */
		int digit = 0;

		while (num >= d && digit < 9) {
			num -= d;
			digit++;
		}
		if (digit || p > 16 || d == 1)
			path[p++] = '0' + digit;
	}
	path[p] = 0;
	fd = sc(SYS_OPEN, (long)path, O_RDONLY | O_CLOEXEC, 0, 0, 0, 0);
	if (fd < 0) {
		klog("cannot open ", path);
		return 1;
	}
	ignore_signals();
	kd_start(&k, now_ms(), key_is_down(fd));
	klog("watching ", path);
	if (k.down)
		klog("key down at start, ignored until released", 0);
	pfd.fd = (int)fd;
	pfd.events = POLLIN;
	for (;;) {
		long r = sc(SYS_POLL, (long)&pfd, 1, kd_timeout(&k, now_ms()), 0, 0, 0);
		int i;

		if (r < 0 && r != -EINTR)
			return 1;
		act(kd_tick(&k, now_ms()));
		if (r <= 0)
			continue;
		n = sc(SYS_READ, fd, (long)ev, sizeof(ev), 0, 0, 0);
		if (n < 0 && n != -EINTR)
			return 1;
		if (n == 0)
			return 1;
		for (i = 0; i < n / (long)sizeof(ev[0]); i++) {
			if (ev[i].type == EV_KEY && ev[i].code == KEY_POWER)
				act(kd_event(&k, ev[i].value, now_ms()));
		}
	}
}

void _start(void) __attribute__((naked));
void _start(void)
{
	__asm__ volatile("bl main\n\tmov r7, #1\n\tsvc 0");
}
