#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/input.h>
#ifndef EV_SYN
#define EV_SYN 0x00
#endif
#ifndef EVIOCGRAB
#define EVIOCGRAB _IOW('E', 0x90, int)
#endif

static unsigned short rgb(int r, int g, int b) { return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); }

int main(int argc, char **argv)
{
	struct fb_var_screeninfo v;
	struct fb_fix_screeninfo f;
	struct input_event ev;
	unsigned short *fb;
	int fd, ts, x, y, cx = 0, cy = 0, down = 0, n = 0;
	char name[64] = "";

	fd = open("/dev/fb0", O_RDWR);
	if (fd < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &v) || ioctl(fd, FBIOGET_FSCREENINFO, &f)) {
		perror("fb");
		return 1;
	}
	printf("fb %s %ux%u %ubpp line %u\n", f.id, v.xres, v.yres, v.bits_per_pixel, f.line_length);
	fb = mmap(0, f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (fb == MAP_FAILED) { perror("mmap"); return 1; }
	for (y = 0; y < (int)v.yres; y++)
		for (x = 0; x < (int)v.xres; x++)
			fb[y * v.xres + x] = (y < 40) ? rgb(0x0e, 0x16, 0x1e) : rgb(x * 255 / v.xres, 0x40, y * 255 / v.yres);
	for (y = 12; y < 28; y++)
		for (x = 12; x < 120; x++)
			fb[y * v.xres + x] = rgb(0x18, 0xbc, 0xf2);
	printf("drew test pattern\n");
	fflush(stdout);
	if (argc < 2) return 0;
	ts = open(argv[1], O_RDONLY);
	if (ts < 0) { perror("ts"); return 1; }
	ioctl(ts, EVIOCGNAME(sizeof name), name);
	printf("touch device '%s', event size %d\n", name, (int)sizeof ev);
	fflush(stdout);
	while (read(ts, &ev, sizeof ev) == sizeof ev) {
		if (ev.type == EV_ABS && ev.code == ABS_X) cx = ev.value;
		if (ev.type == EV_ABS && ev.code == ABS_Y) cy = ev.value;
		if (ev.type == EV_KEY && ev.code == BTN_TOUCH) down = ev.value;
		if (ev.type == EV_SYN && down) {
			for (y = cy - 3; y <= cy + 3; y++)
				for (x = cx - 3; x <= cx + 3; x++)
					if (x >= 0 && y >= 0 && x < (int)v.xres && y < (int)v.yres)
						fb[y * v.xres + x] = 0xffff;
			if (n++ < 20) { printf("touch %d,%d\n", cx, cy); fflush(stdout); }
		}
	}
	return 0;
}
