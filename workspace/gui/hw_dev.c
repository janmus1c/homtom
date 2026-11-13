/* homtom-gui: on-device hardware layer for the GO 630. Freestanding ARMv4T, raw EABI syscalls, no libc.
 * Maps the LCD DMA buffer (phys 0x33F00000, 480x272 RGB565, stride 960) through /dev/mem, draws into a RAM back buffer
 * and blits it. Touch events arrive on stdin as homtom-ts prints them ("x y" raw 10-bit while down, "up" on release;
 * or "t X Y" in screen pixels). The status values come from /tmp/homtom-ui.txt, re-read every second.
 *
 * usage: homtom-gui [-f file] [-p page 0..2] [-n] [-1]
 *   -f file  key=value file (default /tmp/homtom-ui.txt)    -n  do not mute the kernel text console
 *   -1       draw once, leave the picture on the LCD and exit (console stays muted) */
#include "ui.h"

#define FB_PHYS_PAGE 0x33F00	/* 0x33F00000 >> 12 */
#define FB_BYTES (GUI_W * GUI_H * 2)

/* raw ADC readings at the screen edges, used until /mnt/internal/homtom/touch.cal is wired in. Placeholder values. */
#define TS_X0 120
#define TS_X1 900
#define TS_Y0 120
#define TS_Y1 900

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

static u16 back[GUI_W * GUI_H] __attribute__((aligned(4)));
static volatile u32 *lcd;
static volatile int stop;

static void on_signal(int sig)
{
	(void)sig;
	stop = 1;
}

static int sys_open(const char *p, int flags) { return (int)sc(5, (long)p, flags, 0, 0, 0, 0); }
static void sys_close(int fd) { sc(6, fd, 0, 0, 0, 0, 0); }
static int sys_read(int fd, void *b, int n) { return (int)sc(3, fd, (long)b, n, 0, 0, 0); }

static void write_file(const char *path, const char *s)
{
	int fd = sys_open(path, 1), n = 0;

	if (fd < 0)
		return;
	while (s[n])
		n++;
	sc(4, fd, (long)s, n, 0, 0, 0);
	sys_close(fd);
}

static int read_file(const char *path, char *buf, int max)
{
	int fd = sys_open(path, 0), n = 0, r;

	if (fd < 0)
		return -1;
	while (n < max && (r = sys_read(fd, buf + n, max - n)) > 0)
		n += r;
	sys_close(fd);
	return n;
}

static void blit(void)
{
	const u32 *src = (const u32 *)back;
	int i, n = FB_BYTES / 4;

	for (i = 0; i < n; i++)
		lcd[i] = src[i];
}

/* poll(2) on stdin; returns >0 when readable, 0 on timeout, <0 on error/EINTR */
static int stdin_open = 1;

static int wait_stdin(int ms)
{
	struct { int fd; short events, revents; } p = { 0, 1, 0 };

	if (!stdin_open)
		return (int)sc(168, 0, 0, ms, 0, 0, 0);	/* plain sleep */
	return (int)sc(168, (long)&p, 1, ms, 0, 0, 0);
}

static int scale(int v, int lo, int hi, int span)
{
	v = (v - lo) * span / (hi - lo);
	return v < 0 ? 0 : v >= span ? span - 1 : v;
}

static int num(const char **pp, int *out)
{
	const char *p = *pp;
	int v = 0, any = 0;

	while (*p == ' ')
		p++;
	while (*p >= '0' && *p <= '9') {
		v = v * 10 + (*p++ - '0');
		any = 1;
	}
	*pp = p;
	*out = v;
	return any;
}

/* one touch line -> ui_touch; returns 1 when a redraw is needed */
static int touch_line(ui_state *s, const char *l)
{
	int a, b;

	if (l[0] == 'u' && l[1] == 'p')
		return ui_touch(s, 0, 0, 0);
	if (l[0] == 't') {
		l++;
		if (num(&l, &a) && num(&l, &b))
			return ui_touch(s, a, b, 1);
		return 0;
	}
	if (num(&l, &a) && num(&l, &b))
		return ui_touch(s, scale(a, TS_X0, TS_X1, GUI_W), scale(b, TS_Y0, TS_Y1, GUI_H), 1);
	return 0;
}

int main_c(long *sp)
{
	static char data[2048], line[48];
	static ui_state s;
	const char *file = "/tmp/homtom-ui.txt";
	int argc = (int)sp[0], i, once = 0, mute = 1, ll = 0;
	char **argv = (char **)(sp + 1);
	long sa[5] = { (long)on_signal, 0, 0, 0, 0 };
	int fd;
	long m;

	ui_init(&s);
	for (i = 1; i < argc; i++) {
		const char *a = argv[i];

		if (a[0] == '-' && a[1] == 'f' && i + 1 < argc)
			file = argv[++i];
		else if (a[0] == '-' && a[1] == 'p' && i + 1 < argc)
			s.page = argv[++i][0] % UI_PAGES;
		else if (a[0] == '-' && a[1] == 'n')
			mute = 0;
		else if (a[0] == '-' && a[1] == '1')
			once = 1;
	}

	fd = sys_open("/dev/mem", 2 | 0x101000);	/* O_RDWR | O_SYNC */
	if (fd < 0)
		return 2;
	m = sc(192, 0, FB_BYTES, 3, 1, fd, FB_PHYS_PAGE);
	if ((unsigned long)m >= (unsigned long)-4095)
		return 3;
	lcd = (volatile u32 *)m;
	sys_close(fd);

	if (mute)
		write_file("/sys/kernel/homtom_splash", "1\n");
	sc(174, 15, (long)sa, 0, 8, 0, 0);	/* SIGTERM, SIGINT: leave the loop so the console can be restored */
	sc(174, 2, (long)sa, 0, 8, 0, 0);
	gui_begin(back, GUI_W, GUI_H);

	while (!stop) {
		int redraw = 1, n = read_file(file, data, (int)sizeof(data) - 1);

		if (n >= 0)
			ui_parse(&s, data, n);
		else
			s.have = 0;
		for (;;) {
			if (redraw) {
				ui_draw(&s);
				blit();
				redraw = 0;
			}
			if (once || stop)
				break;
			n = wait_stdin(s.press >= 0 || s.drag ? 250 : 1000);
			if (n <= 0)
				break;
			n = sys_read(0, line + ll, (int)sizeof(line) - 1 - ll);
			if (n == 0)
				stdin_open = 0;
			if (n <= 0)
				break;
			ll += n;
			line[ll] = 0;
			for (;;) {
				int e = 0;

				while (e < ll && line[e] != '\n')
					e++;
				if (e == ll)
					break;
				line[e] = 0;
				redraw |= touch_line(&s, line);
				for (n = e + 1; n < ll; n++)
					line[n - e - 1] = line[n];
				ll -= e + 1;
			}
			if (ll >= (int)sizeof(line) - 1)
				ll = 0;
		}
		if (once)
			return 0;
	}
	if (mute)
		write_file("/sys/kernel/homtom_splash", "0\n");
	return 0;
}

__asm__(".globl _start\n_start:\n mov r0, sp\n bl main_c\n mov r7, #1\n svc 0\n");
