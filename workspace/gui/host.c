/* homtom-gui-preview: host-side (macOS/Linux) preview. Same gui.c/ui.c/cfg.c as the device, hardware layer replaced by a
 * PNG writer; actions print "spawn[slot]: argv" and "event: line" on stdout instead of running anything.
 * usage: preview [-c ui.conf] [-f key=value file] [-e ha.states] [-A age_s] [-p page|id] [-t x,y] [-T x,y] [-L x,y]
 *                [-u] [-w ms] [-K short|long] [-s slider_pct] [-z zoom] (-o out.png | -a prefix)
 *   -c file  configurable UI (cfg.c); without it the built-in dashboard (ui.c) is drawn
 *   -e file  Home Assistant states (/tmp/ha.states format); -A age in seconds (default: from the file's mtime)
 *   -E file  a new states file arrives at this point of the sequence (same age)
 *   -t x,y   finger held at x,y (repeatable, applied in order; a later -t moves the finger, e.g. a slider drag)
 *   -T x,y   tap: finger down and up at x,y (runs the action)      -K short|long  power key press
 *   -L x,y   long press: finger down at x,y, 700 ms, finger up    -u  finger up
 *   -S x0,y0,x1,y1  swipe: finger down at x0,y0, five moves to x1,y1 (20 ms apart), finger up
 *   -w ms    let time pass (long press, throttled and delayed HA sends, optimistic values expiring)
 *   -a pre   write every page as pre-<n>-<id>.png instead of -o (hidden pages with slot=* show the first entity
 *            that opens them)
 *   -W secs  device wall clock, UTC seconds since 1970 (header clock, Time & date page); default 0 = clock not set
 *   -Z file  the time zone file (INTERNAL:/homtom/tz); tz:set writes it (FILE.new, then rename)
 *   -P       print the page shown (and whether the screen is locked)
 *   -Q min,rule,secs  print the offset cfg_tz_offset gives (rule 0 none, 1 eu, 2 us) and exit; repeatable
 * The simulated clock advances 10 ms per touch event and only by -w otherwise, so a run is deterministic.
 * Exit status 2 when the config does not parse (the error goes to stderr and the built-in dashboard is drawn). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "cfg.h"

static const char *const slot_name[SLOT_N] = { "backlight", "audio", "cmd", "reboot", "gps", "bt" };

int plat_spawn(int slot, const char *const *argv)
{
	printf("spawn[%s]:", slot_name[slot]);
	for (; *argv; argv++)
		printf(" %s", *argv);
	printf("\n");
	return 0;
}

static const char *tzpath;

int plat_tz_write(const char *val)
{
	char tmp[600];
	FILE *f;

	printf("tz-file: %s\n", val);
	if (!tzpath)
		return 0;
	snprintf(tmp, sizeof(tmp), "%s.new", tzpath);
	f = fopen(tmp, "w");
	if (!f)
		return -1;
	fprintf(f, "%s\n", val);
	if (fclose(f) || rename(tmp, tzpath))
		return -1;
	return 0;
}

void plat_event(const char *line, int len)
{
	printf("event: %.*s", len, line);
}

static u16 back[GUI_W * GUI_H];

static unsigned crc_table[256];

static unsigned crc32(unsigned crc, const unsigned char *p, size_t n)
{
	size_t i;

	crc = ~crc;
	for (i = 0; i < n; i++)
		crc = crc_table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
	return ~crc;
}

static void be32(unsigned char *p, unsigned v)
{
	p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

static void chunk(FILE *f, const char *type, const unsigned char *d, unsigned n)
{
	unsigned char hdr[8], crc[4];
	unsigned c;

	be32(hdr, n);
	memcpy(hdr + 4, type, 4);
	fwrite(hdr, 1, 8, f);
	if (n)
		fwrite(d, 1, n, f);
	c = crc32(0, (const unsigned char *)type, 4);
	if (n)
		c = crc32(c ^ 0, d, n);
	be32(crc, c);
	fwrite(crc, 1, 4, f);
}

/* PNG with stored (uncompressed) deflate blocks: no zlib needed. Scaled by `zoom` with nearest neighbour. */
static int write_png(const char *path, int zoom)
{
	int w = GUI_W * zoom, h = GUI_H * zoom, x, y;
	size_t rowlen = 1 + (size_t)w * 3, raw = rowlen * h, pos = 0, o = 0;
	unsigned char *img = malloc(raw), *z = malloc(raw + raw / 65535 * 5 + 64), ih[13];
	unsigned a = 1, b = 0;
	FILE *f = fopen(path, "wb");

	if (!f || !img || !z)
		return -1;
	for (y = 0; y < h; y++) {
		img[y * rowlen] = 0;
		for (x = 0; x < w; x++) {
			u16 p = back[(y / zoom) * GUI_W + x / zoom];
			unsigned char *d = &img[y * rowlen + 1 + x * 3];

			d[0] = ((p >> 11) & 31) * 255 / 31;
			d[1] = ((p >> 5) & 63) * 255 / 63;
			d[2] = (p & 31) * 255 / 31;
		}
	}
	z[o++] = 0x78; z[o++] = 1;
	while (pos < raw) {
		size_t n = raw - pos > 65535 ? 65535 : raw - pos;

		z[o++] = pos + n == raw;
		z[o++] = n; z[o++] = n >> 8; z[o++] = ~n; z[o++] = (~n) >> 8;
		memcpy(z + o, img + pos, n);
		o += n;
		pos += n;
	}
	for (pos = 0; pos < raw; pos++) {
		a = (a + img[pos]) % 65521;
		b = (b + a) % 65521;
	}
	be32(z + o, b << 16 | a);
	o += 4;
	fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
	be32(ih, w); be32(ih + 4, h);
	ih[8] = 8; ih[9] = 2; ih[10] = ih[11] = ih[12] = 0;
	chunk(f, "IHDR", ih, 13);
	chunk(f, "IDAT", z, (unsigned)o);
	chunk(f, "IEND", NULL, 0);
	fclose(f);
	free(img);
	free(z);
	return 0;
}

static int page_arg(const cfg_ctx *x, const char *a)
{
	int i, n = cfg_page_count(x);

	for (i = 0; i < n; i++)
		if (!strcmp(a, cfg_page_id(x, i)))
			return i;
	i = atoi(a);
	return i >= 0 && i < n ? i : 0;
}

static void set_page(cfg_ctx *x, int p)
{
	x->page = p;
}

/* age -2: from the file's mtime */
static void load_ha(cfg_ctx *x, const char *path, int age)
{
	static char hb[CFG_HA_MAX];
	FILE *f = fopen(path, "r");
	struct stat sb;
	int n = -1;

	if (f) {
		n = (int)fread(hb, 1, sizeof(hb), f);
		fclose(f);
	}
	if (age == -2)
		age = n >= 0 && !stat(path, &sb) ? (int)(time(NULL) - sb.st_mtime) : -1;
	cfg_ha(x, hb, n, age);
}

static void advance(cfg_ctx *x, int ms)
{
	for (; ms > 0; ms -= 50) {
		x->now += ms < 50 ? ms : 50;
		cfg_tick(x);
	}
}

static void touch(cfg_ctx *x, int px, int py, int down)
{
	x->now += 10;
	cfg_touch(x, px, py, down);
	cfg_tick(x);
}

/* -a on a hidden detail page: the slot of the first widget that opens it, for slot=* widgets */
static void template_slot(cfg_ctx *x, int p)
{
	const cfg_conf *c = x->c;
	const char *id = c->pool + c->page[p].id;
	int i;

	for (i = 0; i < c->nwidgets; i++) {
		const cfg_widget *w = &c->w[i];

		if (w->slot && c->pool[w->slot] != '*' &&
		    ((w->hold == A_PAGE && !strcmp(c->pool + w->harg, id)) || (w->action == A_PAGE && !strcmp(c->pool + w->arg, id)))) {
			snprintf(x->dslot, sizeof(x->dslot), "%s", c->pool + w->slot);
			return;
		}
	}
}

int main(int argc, char **argv)
{
	static cfg_ctx x;
	static cfg_conf conf;
	static char buf[CFG_FILE_MAX + 1];
	const char *file = NULL, *out = "preview.png", *cpath = NULL, *all = NULL, *hafile = NULL;
	int i, n = 0, zoom = 1, rc = 0, held = 0, age = -2;
	unsigned c, k;

	for (i = 0; i < 256; i++) {
		c = i;
		for (k = 0; k < 8; k++)
			c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
		crc_table[i] = c;
	}
	cfg_init(&x);
	x.now = 100000;
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-f") && i + 1 < argc)
			file = argv[++i];
		else if (!strcmp(argv[i], "-e") && i + 1 < argc)
			hafile = argv[++i];
		else if (!strcmp(argv[i], "-A") && i + 1 < argc)
			age = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-c") && i + 1 < argc)
			cpath = argv[++i];
		else if (!strcmp(argv[i], "-o") && i + 1 < argc)
			out = argv[++i];
		else if (!strcmp(argv[i], "-a") && i + 1 < argc)
			all = argv[++i];
		else if (!strcmp(argv[i], "-z") && i + 1 < argc)
			zoom = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-s") && i + 1 < argc)
			x.st.slider = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-W") && i + 1 < argc)
			x.wall = (u32)strtoul(argv[++i], NULL, 10);
		else if (!strcmp(argv[i], "-Z") && i + 1 < argc)
			tzpath = argv[++i];
		else if (!strcmp(argv[i], "-Q") && i + 1 < argc) {
			int m, r;
			unsigned long t;

			if (sscanf(argv[++i], "%d,%d,%lu", &m, &r, &t) == 3)
				printf("%d %d %lu %d\n", m, r, t, cfg_tz_offset(m, r, (u32)t));
			n = -1;
		}
	}
	if (n < 0)
		return 0;
	if (tzpath) {
		char tb[32];
		FILE *f = fopen(tzpath, "r");
		int k = f ? (int)fread(tb, 1, sizeof(tb), f) : -1;

		if (f)
			fclose(f);
		cfg_tzfile(&x, tb, k);
	}
	if (cpath) {
		FILE *f = fopen(cpath, "r");

		if (!f) {
			perror(cpath);
			return 1;
		}
		n = (int)fread(buf, 1, sizeof(buf), f);
		fclose(f);
		if (cfg_parse(&conf, buf, n)) {
			cfg_use(&x, &conf);
		} else {
			char num[16];

			snprintf(num, sizeof(num), "%d: ", conf.err_line);
			fprintf(stderr, "%s line %s%s\n", cpath, num, conf.err);
			cfg_set_note(&x, "ui.conf line ", num);
			strncat(x.note, conf.err, sizeof(x.note) - strlen(x.note) - 1);
			rc = 2;
		}
	}
	if (file) {
		FILE *f = fopen(file, "r");

		if (f) {
			n = (int)fread(buf, 1, 2047, f);
			fclose(f);
			cfg_status(&x, buf, n);
		}
	}
	if (hafile)
		load_ha(&x, hafile, age);
	for (i = 1; i < argc; i++) {
		int px, py;

		if (!strcmp(argv[i], "-p") && i + 1 < argc) {
			set_page(&x, page_arg(&x, argv[++i]));
		} else if (!strcmp(argv[i], "-t") && i + 1 < argc && sscanf(argv[++i], "%d,%d", &px, &py) == 2) {
			touch(&x, px, py, 1);
			held = 1;
		} else if ((!strcmp(argv[i], "-T") || !strcmp(argv[i], "-L")) && i + 1 < argc &&
			   sscanf(argv[i + 1], "%d,%d", &px, &py) == 2) {
			if (held)
				touch(&x, 0, 0, 0);
			touch(&x, px, py, 1);
			if (argv[i][1] == 'L')
				advance(&x, 700);
			touch(&x, 0, 0, 0);
			held = 0;
			i++;
		} else if (!strcmp(argv[i], "-S") && i + 1 < argc) {
			int x0, y0, x1, y1, k;

			if (sscanf(argv[++i], "%d,%d,%d,%d", &x0, &y0, &x1, &y1) == 4) {
				if (held)
					touch(&x, 0, 0, 0);
				for (k = 0; k <= 5; k++) {
					touch(&x, x0 + (x1 - x0) * k / 5, y0 + (y1 - y0) * k / 5, 1);
					x.now += 10;
				}
				touch(&x, 0, 0, 0);
				held = 0;
			}
		} else if (!strcmp(argv[i], "-E") && i + 1 < argc) {
			load_ha(&x, argv[++i], age);
		} else if (!strcmp(argv[i], "-w") && i + 1 < argc) {
			advance(&x, atoi(argv[++i]));
		} else if (!strcmp(argv[i], "-P")) {
			printf("page: %s%s\n", cfg_page_id(&x, x.page), x.locked ? " (locked)" : "");
		} else if (!strcmp(argv[i], "-K") && i + 1 < argc) {
			cfg_key(&x, argv[++i]);
		} else if (!strcmp(argv[i], "-u")) {
			touch(&x, 0, 0, 0);
			held = 0;
		}
	}
	gui_begin(back, GUI_W, GUI_H);
	if (all) {
		int np = cfg_page_count(&x);

		for (i = 0; i < np; i++) {
			char path[512];

			set_page(&x, i);
			if (x.c->page[i].hidden)
				template_slot(&x, i);
			snprintf(path, sizeof(path), "%s-%d-%s.png", all, i, cfg_page_id(&x, i));
			cfg_draw(&x);
			if (write_png(path, zoom < 1 ? 1 : zoom)) {
				perror(path);
				return 1;
			}
		}
		return rc;
	}
	cfg_draw(&x);
	if (write_png(out, zoom < 1 ? 1 : zoom)) {
		perror(out);
		return 1;
	}
	return rc;
}
