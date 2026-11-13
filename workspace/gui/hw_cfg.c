/* homtom-gui-cfg: device layer for the configurable UI (cfg.c). Same LCD, touch and status input as homtom-gui (hw_dev.c),
 * plus ui.conf hot reload, the /tmp/ui.events log, child processes for allow-listed actions (fork + execve, no shell)
 * and the Home Assistant states file. Freestanding ARMv4T, raw EABI syscalls, no libc.
 *
 * usage: homtom-gui-cfg [-c ui.conf] [-f status file] [-e ha states] [-p page] [-n] [-1]
 *   -c file  config (default /mnt/internal/homtom/ui.conf; missing or invalid = built-in config, conf/default.conf)
 *   -f file  key=value status (default /tmp/homtom-ui.txt)      -n  do not mute the kernel text console
 *   -e file  Home Assistant states (default /tmp/ha.states, docs/HA_UI.md)
 *   -1       draw once and exit (console stays muted)
 * stdin: "t X Y" (screen px, finger down), "X Y" (raw ADC), "up", "key short", "key long". SIGHUP forces a config reload.
 * A finger held still for 600 ms is a long press: the loop wakes up for it even when no touch line arrives. */
#include "cfg.h"

#define FB_PHYS_PAGE 0x33F00
#define FB_BYTES (GUI_W * GUI_H * 2)
#define TS_X0 120
#define TS_X1 900
#define TS_Y0 120
#define TS_Y1 900
#define EVENTS_MAX 16384	/* stop appending when the bridge has not consumed the file */

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
static volatile int stop, hup;
static int child[SLOT_N];
static char **envp;

static void on_signal(int sig)
{
	if (sig == 1)
		hup = 1;
	else
		stop = 1;
}

static int sys_open(const char *p, int flags, int mode) { return (int)sc(5, (long)p, flags, mode, 0, 0, 0); }
static void sys_close(int fd) { sc(6, fd, 0, 0, 0, 0, 0); }
static int sys_read(int fd, void *b, int n) { return (int)sc(3, fd, (long)b, n, 0, 0, 0); }
static int sys_write(int fd, const void *b, int n) { return (int)sc(4, fd, (long)b, n, 0, 0, 0); }

static void write_file(const char *path, const char *s)
{
	int fd = sys_open(path, 1, 0), n = 0;

	if (fd < 0)
		return;
	while (s[n])
		n++;
	sys_write(fd, s, n);
	sys_close(fd);
}

static int read_file(const char *path, char *buf, int max)
{
	int fd = sys_open(path, 0, 0), n = 0, r;

	if (fd < 0)
		return -1;
	while (n < max && (r = sys_read(fd, buf + n, max - n)) > 0)
		n += r;
	sys_close(fd);
	return n;
}

#define TZ_PATH "/mnt/internal/homtom/tz"

/* INTERNAL is mounted rw,sync: write tz.new on the same volume, then rename over tz */
int plat_tz_write(const char *val)
{
	int fd = sys_open(TZ_PATH ".new", 1 | 0x40 | 0x200, 0644), n = 0;	/* O_WRONLY | O_CREAT | O_TRUNC */

	if (fd < 0)
		return -1;
	while (val[n])
		n++;
	if (sys_write(fd, val, n) != n || sys_write(fd, "\n", 1) != 1) {
		sys_close(fd);
		return -1;
	}
	sc(118, fd, 0, 0, 0, 0, 0);	/* fsync */
	sys_close(fd);
	return sc(38, (long)(TZ_PATH ".new"), (long)TZ_PATH, 0, 0, 0, 0) < 0 ? -1 : 0;	/* rename */
}

void plat_event(const char *line, int len)
{
	int fd = sys_open(CFG_EVENTS, 1 | 0x40 | 0x400, 0644);	/* O_WRONLY | O_CREAT | O_APPEND */

	if (fd < 0)
		return;
	if (sc(19, fd, 0, 2, 0, 0, 0) < EVENTS_MAX)	/* lseek SEEK_END = current size */
		sys_write(fd, line, len);
	sys_close(fd);
}

static void reap(void)
{
	int i, st;
	long pid;

	while ((pid = sc(114, -1, (long)&st, 1, 0, 0, 0)) > 0)	/* wait4(-1, &st, WNOHANG) */
		for (i = 0; i < SLOT_N; i++)
			if (child[i] == pid)
				child[i] = 0;
}

int plat_spawn(int slot, const char *const *argv)
{
	long pid;

	reap();
	if (child[slot] || (slot != SLOT_REBOOT && child[SLOT_REBOOT]))
		return -1;
	pid = sc(2, 0, 0, 0, 0, 0, 0);	/* fork */
	if (pid < 0)
		return -1;
	if (pid == 0) {
		int fd = sys_open("/dev/null", 2, 0);

		if (fd >= 0) {
			sc(63, fd, 0, 0, 0, 0, 0);
			sc(63, fd, 1, 0, 0, 0, 0);
			sc(63, fd, 2, 0, 0, 0, 0);
		}
		sc(11, (long)argv[0], (long)argv, (long)envp, 0, 0, 0);
		sc(1, 127, 0, 0, 0, 0, 0);
	}
	child[slot] = (int)pid;
	return 0;
}

static u32 wall_s(void)
{
	long ts[2];

	sc(263, 0, (long)ts, 0, 0, 0, 0);	/* CLOCK_REALTIME */
	return (u32)ts[0];
}

static u32 clock_ms(void)
{
	long ts[2];

	sc(263, 1, (long)ts, 0, 0, 0, 0);	/* clock_gettime(CLOCK_MONOTONIC) */
	return (u32)ts[0] * 1000 + (u32)ts[1] / 1000000;
}

/* seconds since path was modified (wall clock, as the writer stamps it); -1 when it cannot be stat'ed. A time more than
   CFG_HA_STALE seconds in the future (clock set back) counts as stale */
static int file_age(const char *path)
{
	long st[26], ts[2];	/* struct stat64 of ARM EABI: 104 bytes, st_mtime at byte 80 */
	int age;

	if (sc(195, (long)path, (long)st, 0, 0, 0, 0) < 0)	/* stat64 */
		return -1;
	sc(263, 0, (long)ts, 0, 0, 0, 0);	/* CLOCK_REALTIME */
	age = (int)(ts[0] - st[20]);
	return age < -CFG_HA_STALE ? 99999 : age < 0 ? 0 : age;
}

static void blit(void)
{
	const u32 *src = (const u32 *)back;
	int i, n = FB_BYTES / 4;

	for (i = 0; i < n; i++)
		lcd[i] = src[i];
}

static int stdin_eof;	/* zero-initialised: an initialised static would add a page-aligned data segment to the file */
static int gui_ready;	/* set after the first frame reached the LCD; /tmp/gui.ready is the init boot watchdog's marker */

static void touch_file(const char *path)
{
	int fd = sys_open(path, 01 | 0100 | 01000, 0644);	/* O_WRONLY | O_CREAT | O_TRUNC */

	if (fd >= 0)
		sys_close(fd);
}

static int wait_stdin(int ms)
{
	struct { int fd; short events, revents; } p = { 0, 1, 0 };

	if (stdin_eof)
		return (int)sc(168, 0, 0, ms, 0, 0, 0);
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

static int input_line(cfg_ctx *x, const char *l)
{
	int a, b;

	if (l[0] == 'u' && l[1] == 'p')
		return cfg_touch(x, 0, 0, 0);
	if (l[0] == 'k' && l[1] == 'e' && l[2] == 'y' && l[3] == ' ')
		return cfg_key(x, l + 4);
	if (l[0] == 't') {
		l++;
		if (num(&l, &a) && num(&l, &b))
			return cfg_touch(x, a, b, 1);
		return 0;
	}
	if (num(&l, &a) && num(&l, &b))
		return cfg_touch(x, scale(a, TS_X0, TS_X1, GUI_W), scale(b, TS_Y0, TS_Y1, GUI_H), 1);
	return 0;
}

static cfg_conf conf_a, conf_b;
static u32 conf_hash;

/* (re)load the config when its contents changed; returns 1 when the screen must be redrawn */
static int load_conf(cfg_ctx *x, const char *path, int force)
{
	static char buf[CFG_FILE_MAX];
	static int loaded;
	char msg[CFG_ERR], *p;
	cfg_conf *spare;
	int n = read_file(path, buf, CFG_FILE_MAX);
	u32 h;

	if (n < 0) {
		if (!x->builtin) {
			cfg_use(x, 0);
			cfg_set_note(x, "ui.conf gone: built-in config", 0);
			conf_hash = 0;
			return 1;
		}
		return 0;
	}
	h = cfg_hash(buf, n);
	if (h == conf_hash && !force)
		return 0;
	conf_hash = h;
	spare = x->c == &conf_a ? &conf_b : &conf_a;
	if (cfg_parse(spare, buf, n)) {
		cfg_use(x, spare);
		cfg_set_note(x, loaded ? "ui.conf reloaded" : 0, 0);
		cfg_emit(x, "reload", "ui.conf", 1);
		loaded = 1;
	} else {
		p = gui_cat(msg, "ui.conf line ");
		p = gui_utoa(p, (u32)spare->err_line);
		gui_cat(p, ": ");
		cfg_set_note(x, msg, spare->err);
		cfg_emit(x, "reload", "ui.conf", 0);
	}
	return 1;
}

int main_c(long *sp)
{
	static char data[2048], line[48], hbuf[CFG_HA_MAX];
	static cfg_ctx x;
	const char *file = "/tmp/homtom-ui.txt", *conf = CFG_PATH, *hafile = CFG_HA_PATH;
	int argc = (int)sp[0], i, once = 0, mute = 1, ll = 0, page = -1, tick = 0;
	char **argv = (char **)(sp + 1);
	long sa[5] = { (long)on_signal, 0, 0, 0, 0 };
	int fd;
	long m;

	envp = argv + argc + 1;
	cfg_init(&x);
	for (i = 1; i < argc; i++) {
		const char *a = argv[i];

		if (a[0] == '-' && a[1] == 'f' && i + 1 < argc)
			file = argv[++i];
		else if (a[0] == '-' && a[1] == 'c' && i + 1 < argc)
			conf = argv[++i];
		else if (a[0] == '-' && a[1] == 'e' && i + 1 < argc)
			hafile = argv[++i];
		else if (a[0] == '-' && a[1] == 'p' && i + 1 < argc)
			page = argv[++i][0] - '0';
		else if (a[0] == '-' && a[1] == 'n')
			mute = 0;
		else if (a[0] == '-' && a[1] == '1')
			once = 1;
	}

	fd = sys_open("/dev/mem", 2 | 0x101000, 0);
	if (fd < 0)
		return 2;
	m = sc(192, 0, FB_BYTES, 3, 1, fd, FB_PHYS_PAGE);
	if ((unsigned long)m >= (unsigned long)-4095)
		return 3;
	lcd = (volatile u32 *)m;
	sys_close(fd);

	if (mute)
		write_file("/sys/kernel/homtom_splash", "1\n");
	sc(174, 15, (long)sa, 0, 8, 0, 0);
	sc(174, 2, (long)sa, 0, 8, 0, 0);
	sc(174, 1, (long)sa, 0, 8, 0, 0);	/* SIGHUP: reload ui.conf */
	gui_begin(back, GUI_W, GUI_H);
	load_conf(&x, conf, 1);
	if (page >= 0 && page < cfg_page_count(&x))
		x.page = page;
	cfg_emit(&x, "start", "gui", !x.builtin);

	while (!stop) {
		int redraw = 1, n = read_file(file, data, (int)sizeof(data) - 1);

		u32 t_ref;

		if (n >= 0)
			cfg_status(&x, data, n);
		else
			x.st.have = 0;
		n = read_file(hafile, hbuf, CFG_HA_MAX);
		cfg_ha(&x, hbuf, n, n < 0 ? -1 : file_age(hafile));
		if (hup || x.reload || (++tick & 1) == 0) {
			static char tzb[32];

			load_conf(&x, conf, hup || x.reload);
			hup = x.reload = 0;
			n = read_file(TZ_PATH, tzb, (int)sizeof(tzb));
			cfg_tzfile(&x, tzb, n);
		}
		reap();
		x.now = t_ref = clock_ms();
		x.wall = wall_s();
		cfg_tick(&x);
		for (;;) {
			int left;

			/* locked: the backlight is off; nothing is drawn until the unlock asks for a redraw (or a hold hint shows) */
			if (redraw && (!x.locked || x.ovl)) {
				cfg_draw(&x);
				blit();
				if (!gui_ready) {
					gui_ready = 1;
					touch_file("/tmp/gui.ready");
				}
			}
			redraw = 0;
			if (once || stop || hup || x.reload)
				break;
			/* files are re-read every second, every 250 ms while a finger is down; timers (long press, a waiting HA
			   send) wake the loop in between */
			left = (x.press >= 0 || x.drag >= 0 ? 250 : 1000) - (int)(x.now - t_ref);
			if (x.locked && left > 0)
				left += 1000;	/* locked: files every 2 s, no redraws */
			if (left <= 0)
				break;
			n = cfg_wait(&x);
			n = wait_stdin(left < n ? left : n);
			x.now = clock_ms();
			redraw |= cfg_tick(&x);
			if (n == 0)
				continue;
			if (n < 0)
				break;
			n = sys_read(0, line + ll, (int)sizeof(line) - 1 - ll);
			if (n == 0)
				stdin_eof = 1;
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
				redraw |= input_line(&x, line);
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
	/* no unmute at exit: a respawn must not flash the boot log; init and homtom-ui-start unmute on real failures */
	return 0;
}

__asm__(".globl _start\n_start:\n mov r0, sp\n bl main_c\n mov r7, #1\n svc 0\n");
