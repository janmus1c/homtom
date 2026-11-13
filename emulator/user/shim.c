/*
 * LD_PRELOAD shim for the userspace emulator. Plain files stand in for kernel devices:
 * /dev/fb (480x272 RGB565 framebuffer), /dev/input/event0 (FIFO of evdev events),
 * /dev/watchdog (arms a reset in the supervisor). Paths under /proc that only the
 * TomTom kernel provides are redirected to /.emu/proc. The device glibc has no libdl, so
 * wrapped libc functions are found through dl_iterate_phdr and libc's DT_HASH table.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/utsname.h>

extern FILE *_IO_fopen(const char *, const char *);
#include <linux/fb.h>
#include <linux/input.h>
#ifndef EV_SYN
#define EV_SYN 0x00
#endif
#ifndef EVIOCGRAB
#define EVIOCGRAB _IOW('E', 0x90, int)
#endif
#define WDIOC_GETSUPPORT _IOR('W', 0, int[10])
#define WDIOC_KEEPALIVE _IOR('W', 5, int)
#define WDIOC_SETTIMEOUT _IOWR('W', 6, int)
#define WDIOC_GETTIMEOUT _IOR('W', 7, int)

#define W 480
#define H 272
#define EVENTS "/.emu/events"

struct node { const char *path; dev_t dev; ino_t ino; int ok; };
static struct node fb = { "/dev/homtom-fb" }, ts = { "/dev/homtom-ts" }, wdt = { "/dev/homtom-wdt" };
static struct fb_var_screeninfo var;
static int wdt_timeout = 60;

static const char *redirect[] = {
	"/proc/barcelona", "/proc/partitions", "/proc/mtd", "/proc/cmdline", 0
};

struct sym_query { const char *name; void *addr; };

static unsigned long elf_hash(const char *name)
{
	unsigned long h = 0, g;
	while (*name) {
		h = (h << 4) + (unsigned char)*name++;
		if ((g = h & 0xf0000000)) h ^= g >> 24;
		h &= ~g;
	}
	return h;
}

static int find_in(struct dl_phdr_info *info, size_t size, void *data)
{
	struct sym_query *q = data;
	Elf32_Addr base = info->dlpi_addr, hash = 0, symtab = 0, strtab = 0;
	Elf32_Dyn *d = 0;
	Elf32_Word *h, *chain;
	Elf32_Sym *syms;
	const char *strs;
	int i;
	if (!info->dlpi_name || !strstr(info->dlpi_name, "libc.so")) return 0;
	for (i = 0; i < info->dlpi_phnum; i++)
		if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) d = (Elf32_Dyn *)(base + info->dlpi_phdr[i].p_vaddr);
	for (; d && d->d_tag != DT_NULL; d++) {
		Elf32_Addr v = d->d_un.d_ptr < base ? d->d_un.d_ptr + base : d->d_un.d_ptr;
		if (d->d_tag == DT_HASH) hash = v;
		if (d->d_tag == DT_SYMTAB) symtab = v;
		if (d->d_tag == DT_STRTAB) strtab = v;
	}
	if (!hash || !symtab || !strtab) return 0;
	h = (Elf32_Word *)hash;
	syms = (Elf32_Sym *)symtab;
	strs = (const char *)strtab;
	chain = h + 2 + h[0];
	for (i = h[2 + elf_hash(q->name) % h[0]]; i; i = chain[i])
		if (syms[i].st_value && !strcmp(strs + syms[i].st_name, q->name)) {
			q->addr = (void *)(base + syms[i].st_value);
			return 1;
		}
	return 0;
}

static void *libc_sym(const char *name)
{
	struct sym_query q = { name, 0 };
	dl_iterate_phdr(find_in, &q);
	if (!q.addr) abort();
	return q.addr;
}

static void lookup(struct node *n)
{
	struct stat st;
	if (stat(n->path, &st) == 0) { n->dev = st.st_dev; n->ino = st.st_ino; n->ok = 1; }
}

static void __attribute__((constructor)) init(void)
{
	lookup(&fb);
	lookup(&ts);
	lookup(&wdt);
	if (getenv("HOMTOM_EMU_LINEBUF")) setvbuf(stdout, 0, _IOLBF, 0);
	memset(&var, 0, sizeof var);
	var.xres = var.xres_virtual = W;
	var.yres = var.yres_virtual = H;
	var.bits_per_pixel = 16;
	var.red.offset = 11; var.red.length = 5;
	var.green.offset = 5; var.green.length = 6;
	var.blue.offset = 0; var.blue.length = 5;
	var.height = 54; var.width = 95;
	var.pixclock = 111111;
	var.activate = FB_ACTIVATE_NOW;
}

static int is(int fd, struct node *n)
{
	struct stat st;
	return n->ok && fstat(fd, &st) == 0 && st.st_dev == n->dev && st.st_ino == n->ino;
}

static const char *map(const char *path, char *buf, int len)
{
	int i;
	if (!path || strncmp(path, "/proc/", 6))
		return path;
	for (i = 0; redirect[i]; i++)
		if (!strncmp(path, redirect[i], strlen(redirect[i]))) {
			snprintf(buf, len, "/.emu%s", path);
			return buf;
		}
	return path;
}

static void event(const char *fmt, int value)
{
	char line[64];
	int fd = syscall(SYS_open, EVENTS, O_WRONLY | O_APPEND | O_CREAT, 0644);
	if (fd < 0) return;
	snprintf(line, sizeof line, fmt, value);
	write(fd, line, strlen(line));
	close(fd);
}

/* The kernel copies at most the bitmap's own size, whatever length the caller passes. */
static int set_bits(void *arg, unsigned len, unsigned max, const int *bits, int n)
{
	unsigned size = ((max + 32) / 32) * 4;
	int k;
	if (len > size) len = size;
	memset(arg, 0, len);
	for (k = 0; k < n; k++)
		if ((unsigned)bits[k] / 8 < len)
			((unsigned char *)arg)[bits[k] / 8] |= 1 << (bits[k] % 8);
	return len;
}

static int fb_ioctl(unsigned long req, void *arg)
{
	struct fb_fix_screeninfo *fix;
	switch (req) {
	case FBIOGET_VSCREENINFO:
		memcpy(arg, &var, sizeof var);
		return 0;
	case FBIOPUT_VSCREENINFO:
	case FBIOPAN_DISPLAY:
	case FBIOGETCMAP:
	case FBIOPUTCMAP:
	case FBIOBLANK:
		return 0;
	case FBIOGET_FSCREENINFO:
		fix = arg;
		memset(fix, 0, sizeof *fix);
		strcpy(fix->id, "ttgfb-emu");
		fix->smem_len = W * H * 2;
		fix->type = FB_TYPE_PACKED_PIXELS;
		fix->visual = FB_VISUAL_TRUECOLOR;
		fix->line_length = W * 2;
		return 0;
	}
	errno = EINVAL;
	return -1;
}

static int ev_ioctl(unsigned long req, void *arg)
{
	static const int evbits[] = { EV_SYN, EV_KEY, EV_ABS };
	static const int keybits[] = { BTN_TOUCH };
	static const int absbits[] = { ABS_X, ABS_Y, ABS_PRESSURE };
	static const char name[] = "homtom-emu-touch";
	unsigned nr = _IOC_NR(req), len = _IOC_SIZE(req);

	if (req == EVIOCGVERSION) { *(int *)arg = EV_VERSION; return 0; }
	if (req == EVIOCGID) { memset(arg, 0, 8); return 0; }
	if (_IOC_TYPE(req) != 'E') { errno = EINVAL; return -1; }
	if (nr == _IOC_NR(EVIOCGNAME(0))) {
		if (len > sizeof name) len = sizeof name;
		memcpy(arg, name, len);
		return len;
	}
	if (nr == 0x20) return set_bits(arg, len, 0x1f, evbits, 3);
	if (nr == 0x20 + EV_KEY) return set_bits(arg, len, 0x1ff, keybits, 1);
	if (nr == 0x20 + EV_ABS) return set_bits(arg, len, 0x3f, absbits, 3);
	if (nr > 0x20 && nr < 0x40) return set_bits(arg, len, 0x1f, 0, 0);
	if (nr >= 0x40 && nr < 0x40 + ABS_MAX) {
		int *a = arg;
		memset(a, 0, 5 * sizeof(int));
		a[2] = (nr - 0x40 == ABS_PRESSURE) ? 255 : 1023;
		return 0;
	}
	if (nr == _IOC_NR(EVIOCGRAB)) return 0;
	errno = EINVAL;
	return -1;
}

static int wdt_ioctl(unsigned long req, void *arg)
{
	switch (req) {
	case WDIOC_GETSUPPORT:
		memset(arg, 0, 40);
		return 0;
	case WDIOC_SETTIMEOUT:
		wdt_timeout = *(int *)arg;
		event("wdt-timeout %d\n", wdt_timeout);
		return 0;
	case WDIOC_GETTIMEOUT:
		*(int *)arg = wdt_timeout;
		return 0;
	case WDIOC_KEEPALIVE:
		event("wdt-keepalive %d\n", wdt_timeout);
		return 0;
	}
	errno = EINVAL;
	return -1;
}

int ioctl(int fd, unsigned long req, ...)
{
	va_list ap;
	void *arg;
	va_start(ap, req);
	arg = va_arg(ap, void *);
	va_end(ap);
	if (is(fd, &fb)) return fb_ioctl(req, arg);
	if (is(fd, &ts)) return ev_ioctl(req, arg);
	if (is(fd, &wdt)) return wdt_ioctl(req, arg);
	return syscall(SYS_ioctl, fd, req, arg);
}

static int do_open(const char *path, int flags, va_list ap)
{
	char buf[256];
	int mode = (flags & O_CREAT) ? va_arg(ap, int) : 0;
	int fd = syscall(SYS_open, map(path, buf, sizeof buf), flags, mode);
	if (fd >= 0 && is(fd, &wdt))
		event("wdt-open %d\n", wdt_timeout);
	return fd;
}

int open(const char *path, int flags, ...)
{
	va_list ap;
	int fd;
	va_start(ap, flags);
	fd = do_open(path, flags, ap);
	va_end(ap);
	return fd;
}

int open64(const char *path, int flags, ...)
{
	va_list ap;
	int fd;
	va_start(ap, flags);
	fd = do_open(path, flags | O_LARGEFILE, ap);
	va_end(ap);
	return fd;
}

FILE *fopen(const char *path, const char *mode)
{
	char buf[256];
	return _IO_fopen(map(path, buf, sizeof buf), mode);
}

FILE *fopen64(const char *path, const char *mode)
{
	static FILE *(*real)(const char *, const char *);
	char buf[256];
	if (!real) real = libc_sym("fopen64");
	return real(map(path, buf, sizeof buf), mode);
}

DIR *opendir(const char *path)
{
	static DIR *(*real)(const char *);
	char buf[256];
	if (!real) real = libc_sym("opendir");
	return real(map(path, buf, sizeof buf));
}

#define WRAP_STAT(fn, type) \
int fn(int ver, const char *path, type *st) \
{ \
	static int (*real)(int, const char *, type *); \
	char buf[256]; \
	if (!real) real = libc_sym(#fn); \
	return real(ver, map(path, buf, sizeof buf), st); \
}
WRAP_STAT(__xstat, struct stat)
WRAP_STAT(__lxstat, struct stat)
WRAP_STAT(__xstat64, struct stat64)
WRAP_STAT(__lxstat64, struct stat64)

int access(const char *path, int mode)
{
	char buf[256];
	return syscall(SYS_access, map(path, buf, sizeof buf), mode);
}

/* The host kernel shows through qemu-user; report the device's. */
int uname(struct utsname *u)
{
	if (syscall(SYS_uname, u) < 0) return -1;
	strcpy(u->release, "2.6.13");
	strcpy(u->version, "#1 PREEMPT homtom-emu");
	strcpy(u->machine, "armv4tl");
	return 0;
}
