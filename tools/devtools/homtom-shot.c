/* homtom-shot: read-only screenshot of the TomTom GO 630 LCD (S3C2443, legacy LCD controller at 0x4D000000).
 *
 * There is no /dev/fb0. The tool maps the LCD register page read-only through /dev/mem, takes the scan-out
 * address from LCDSADDR1 (<< 1), the size from LCDCON2/LCDCON3, the stride from LCDSADDR3 and the halfword order
 * from LCDCON5, then maps that RAM read-only (O_SYNC, so uncached) and copies it out. Nothing is ever written to
 * device memory. If the registers look wrong it falls back to 0x33F00000, 480x272, stride 960 (the buffer the
 * homtom kernel reserves in mach-homtom-go630.c) and says so on stderr.
 *
 * usage: homtom-shot [-o FILE] [-f hts|raw|bmp] [-s 1|2|4] [-l MS [-t S]] [-i]
 *   -o FILE  output path (default "-" = stdout). A file is written as FILE.tmp and renamed, so a reader never
 *            sees half a frame.
 *   -f hts   (default) 24-byte header + RGB565 run-length payload, see below
 *      raw   24-byte header + RGB565 little-endian pixels
 *      bmp   24-bit BMP, readable by any viewer
 *   -s N     box-average downscale by 1, 2 or 4
 *   -l MS    loop: write a frame every MS milliseconds (min 100); only with -o FILE
 *   -t S     loop duration cap in seconds (default 300, max 3600)
 *   -i       print the LCD registers and derived geometry, write no image
 *
 * HTS header (little endian): "HTS1", u16 w, u16 h, u8 fmt (0 raw, 1 rle), u8 scale, u16 flags
 * (bit0 TPAL overlay active, bit1 LCD output disabled, bit2 fallback geometry), u32 TPAL, u32 phys, u32 seq.
 * RLE payload, pixels in row order: control byte c < 128 = c+1 literal pixels follow; c >= 128 = the next pixel
 * repeats c-126 times (2..129).
 *
 * Freestanding ARMv4T EABI, raw syscalls, no libc, no libgcc, no divisions.
 * Build: tools/devtools/build.sh homtom-shot
 */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#define SYS_EXIT	1
#define SYS_WRITE	4
#define SYS_OPEN	5
#define SYS_CLOSE	6
#define SYS_RENAME	38
#define SYS_MUNMAP	91
#define SYS_NANOSLEEP	162
#define SYS_MMAP2	192

#define O_WRONLY	1
#define O_CREAT		0100
#define O_TRUNC		01000
#define O_RDONLY	0
#define O_SYNC		04010000
#define PROT_READ	1
#define MAP_SHARED	1

#define LCD_PA		0x4D000000u
#define FB_FALLBACK	0x33F00000u
#define RAM_START	0x30000000u
#define RAM_END		0x34000000u
#define MAX_W		800
#define MAX_H		480
#define HDR		24

#define F_TPAL		1
#define F_OFF		2
#define F_FALLBACK	4

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

#define IS_ERR(v)	((unsigned long)(v) >= (unsigned long)-4096)

/* worst case: BMP 800*480*3 + 54, or RLE with one control byte per 128 literals */
static u8 out[MAX_W * MAX_H * 3 + 64];
static u16 row[MAX_W];
static u32 acc[3][MAX_W];

static u32 slen(const char *s)
{
	u32 n = 0;

	while (s[n])
		n++;
	return n;
}

static void put(long fd, const char *s)
{
	sc(SYS_WRITE, fd, (long)s, slen(s), 0, 0, 0);
}

static void hex(long fd, const char *name, u32 v)
{
	char b[32];
	u32 i, n = slen(name);

	for (i = 0; i < n && i < 16; i++)
		b[i] = name[i];
	b[i++] = '=';
	b[i++] = '0';
	b[i++] = 'x';
	for (n = 0; n < 8; n++)
		b[i++] = "0123456789abcdef"[(v >> (28 - 4 * n)) & 15];
	b[i++] = ' ';
	b[i] = 0;
	put(fd, b);
}

static void dec(long fd, const char *name, u32 v)
{
	static const u32 p10[] = { 1000000000, 100000000, 10000000, 1000000, 100000, 10000, 1000, 100, 10, 1 };
	char b[32];
	u32 i, k, n = slen(name), started = 0;

	for (i = 0; i < n && i < 16; i++)
		b[i] = name[i];
	b[i++] = '=';
	for (k = 0; k < 10; k++) {
		char d = '0';

		while (v >= p10[k]) {
			v -= p10[k];
			d++;
		}
		if (d != '0' || started || k == 9) {
			b[i++] = d;
			started = 1;
		}
	}
	b[i++] = ' ';
	b[i] = 0;
	put(fd, b);
}

static u32 num(const char *s)
{
	u32 v = 0;

	while (*s >= '0' && *s <= '9')
		v = v * 10 + (u32)(*s++ - '0');
	return v;
}

static int eq(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

static void le16(u8 *p, u32 v)
{
	p[0] = v;
	p[1] = v >> 8;
}

static void le32(u8 *p, u32 v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

static int write_all(long fd, const u8 *p, u32 n)
{
	while (n) {
		long w = sc(SYS_WRITE, fd, (long)p, n, 0, 0, 0);

		if (w <= 0)
			return 0;
		p += w;
		n -= (u32)w;
	}
	return 1;
}

struct geo {
	u32 phys, w, h, stride, hwswp, flags, tpal;
	u32 reg[9];
};

static volatile u32 *lcdregs;

static int probe(struct geo *g, int quiet)
{
	volatile u32 *r;
	u32 i, bpp;

	if (!lcdregs) {
		long fd = sc(SYS_OPEN, (long)"/dev/mem", O_RDONLY | O_SYNC, 0, 0, 0, 0), m;

		if (fd < 0) {
			put(2, "homtom-shot: cannot open /dev/mem\n");
			return 0;
		}
		m = sc(SYS_MMAP2, 0, 4096, PROT_READ, MAP_SHARED, fd, LCD_PA >> 12);
		sc(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
		if (IS_ERR(m)) {
			put(2, "homtom-shot: cannot map the LCD registers\n");
			return 0;
		}
		lcdregs = (volatile u32 *)m;
	}
	r = lcdregs;
	for (i = 0; i < 8; i++)
		g->reg[i] = r[i];
	g->reg[8] = r[0x50 / 4];
	g->flags = 0;
	g->tpal = g->reg[8];
	if (g->tpal & (1u << 24))
		g->flags |= F_TPAL;
	if (!(g->reg[0] & 1))
		g->flags |= F_OFF;
	bpp = (g->reg[0] >> 1) & 15;
	g->phys = (g->reg[5] & 0x3fffffffu) << 1;
	g->h = ((g->reg[1] >> 14) & 0x3ff) + 1;
	g->w = ((g->reg[2] >> 8) & 0x7ff) + 1;
	g->stride = ((g->reg[7] & 0x7ff) + ((g->reg[7] >> 11) & 0x7ff)) * 2;
	g->hwswp = g->reg[4] & 1;
	if (bpp != 12 || g->w > MAX_W || g->h > MAX_H || g->stride < g->w * 2 || g->phys < RAM_START ||
	    g->phys + g->stride * g->h > RAM_END) {
		if (!quiet)
			put(2, "homtom-shot: LCD registers do not describe a 16 bpp RAM framebuffer; using 0x33f00000 480x272\n");
		g->phys = FB_FALLBACK;
		g->w = 480;
		g->h = 272;
		g->stride = 960;
		g->hwswp = 1;
		g->flags |= F_FALLBACK;
	}
	if ((g->flags & F_TPAL) && !quiet)
		put(2, "homtom-shot: TPAL overlay is on; the panel shows a solid colour, not this framebuffer\n");
	return 1;
}

static volatile u32 *fbmap;
static u32 fbmap_phys, fbmap_len;
static long fbmap_base;

static int map_fb(const struct geo *g)
{
	u32 pg = g->phys & ~0xfffu, len = (g->phys - pg) + g->stride * g->h;
	long fd, m;

	if (fbmap && fbmap_phys == g->phys)
		return 1;
	fd = sc(SYS_OPEN, (long)"/dev/mem", O_RDONLY | O_SYNC, 0, 0, 0, 0);
	if (fd < 0)
		return 0;
	m = sc(SYS_MMAP2, 0, (len + 0xfff) & ~0xfffu, PROT_READ, MAP_SHARED, fd, pg >> 12);
	sc(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
	if (IS_ERR(m)) {
		put(2, "homtom-shot: cannot map the framebuffer\n");
		return 0;
	}
	fbmap = (volatile u32 *)(m + (g->phys - pg));
	fbmap_phys = g->phys;
	fbmap_base = m;
	fbmap_len = (len + 0xfff) & ~0xfffu;
	return 1;
}

/* one source row into row[], in screen order; 32-bit uncached reads */
static void read_row(const struct geo *g, u32 y)
{
	volatile u32 *p = fbmap + ((g->stride * y) >> 2);
	u32 x, n = (g->w + 1) >> 1;

	for (x = 0; x < n; x++) {
		u32 v = p[x];

		if (g->hwswp) {
			row[2 * x] = v;
			row[2 * x + 1] = v >> 16;
		} else {
			row[2 * x] = v >> 16;
			row[2 * x + 1] = v;
		}
	}
}

static u32 sh_of(u32 s)
{
	return s == 4 ? 2 : s == 2 ? 1 : 0;
}

/* next output row (downscaled) into row[0..ow) */
static void next_row(const struct geo *g, u32 oy, u32 scale, u32 ow)
{
	u32 sh = sh_of(scale), x, k, j;

	if (scale == 1) {
		read_row(g, oy);
		return;
	}
	for (x = 0; x < ow; x++)
		acc[0][x] = acc[1][x] = acc[2][x] = 0;
	for (j = 0; j < scale; j++) {
		read_row(g, (oy << sh) + j);
		for (x = 0; x < ow; x++)
			for (k = 0; k < scale; k++) {
				u32 p = row[(x << sh) + k];

				acc[0][x] += p >> 11;
				acc[1][x] += (p >> 5) & 63;
				acc[2][x] += p & 31;
			}
	}
	for (x = 0; x < ow; x++)
		row[x] = ((acc[0][x] >> (2 * sh)) << 11) | ((acc[1][x] >> (2 * sh)) << 5) | (acc[2][x] >> (2 * sh));
}

enum { FMT_HTS, FMT_RAW, FMT_BMP };

static u32 header(const struct geo *g, int fmt, u32 scale, u32 seq, u32 ow, u32 oh)
{
	out[0] = 'H';
	out[1] = 'T';
	out[2] = 'S';
	out[3] = '1';
	le16(out + 4, ow);
	le16(out + 6, oh);
	out[8] = fmt == FMT_HTS;
	out[9] = scale;
	le16(out + 10, g->flags);
	le32(out + 12, g->tpal);
	le32(out + 16, g->phys);
	le32(out + 20, seq);
	return HDR;
}

static u32 encode_bmp(const struct geo *g, u32 scale)
{
	u32 sh = sh_of(scale), ow = g->w >> sh, oh = g->h >> sh, rb = (ow * 3 + 3) & ~3u, x, y;

	out[0] = 'B';
	out[1] = 'M';
	le32(out + 2, 54 + rb * oh);
	le32(out + 6, 0);
	le32(out + 10, 54);
	le32(out + 14, 40);
	le32(out + 18, ow);
	le32(out + 22, oh);
	le16(out + 26, 1);
	le16(out + 28, 24);
	le32(out + 30, 0);
	le32(out + 34, rb * oh);
	le32(out + 38, 2835);
	le32(out + 42, 2835);
	le32(out + 46, 0);
	le32(out + 50, 0);
	for (y = 0; y < oh; y++) {
		u8 *d = out + 54 + rb * (oh - 1 - y);

		next_row(g, y, scale, ow);
		for (x = 0; x < ow; x++) {
			u32 p = row[x], r = p >> 11, gg = (p >> 5) & 63, b = p & 31;

			d[3 * x] = (b << 3) | (b >> 2);
			d[3 * x + 1] = (gg << 2) | (gg >> 4);
			d[3 * x + 2] = (r << 3) | (r >> 2);
		}
		for (x = ow * 3; x < rb; x++)
			d[x] = 0;
	}
	return 54 + rb * oh;
}

static u32 encode_raw(const struct geo *g, u32 scale, u32 seq)
{
	u32 sh = sh_of(scale), ow = g->w >> sh, oh = g->h >> sh, n, x, y;

	n = header(g, FMT_RAW, scale, seq, ow, oh);
	for (y = 0; y < oh; y++) {
		next_row(g, y, scale, ow);
		for (x = 0; x < ow; x++) {
			out[n++] = row[x];
			out[n++] = row[x] >> 8;
		}
	}
	return n;
}

/* RLE state: an open literal block (lit pixels, control byte at litpos) and a pending run of `cur` */
static u32 n_out, lit, litpos, run, cur;

static void flush_pending(void)
{
	if (run >= 2) {
		if (lit) {
			out[litpos] = lit - 1;
			lit = 0;
		}
		out[n_out++] = 126 + run;
		out[n_out++] = cur;
		out[n_out++] = cur >> 8;
	} else if (run == 1) {
		if (!lit)
			litpos = n_out++;
		out[n_out++] = cur;
		out[n_out++] = cur >> 8;
		if (++lit == 128) {
			out[litpos] = 127;
			lit = 0;
		}
	}
	run = 0;
}

static u32 encode_rle(const struct geo *g, u32 scale, u32 seq)
{
	u32 sh = sh_of(scale), ow = g->w >> sh, oh = g->h >> sh, x, y;

	n_out = header(g, FMT_HTS, scale, seq, ow, oh);
	lit = run = cur = 0;
	for (y = 0; y < oh; y++) {
		next_row(g, y, scale, ow);
		for (x = 0; x < ow; x++) {
			u32 p = row[x];

			if (run && p == cur && run < 129) {
				run++;
				continue;
			}
			flush_pending();
			cur = p;
			run = 1;
		}
	}
	flush_pending();
	if (lit)
		out[litpos] = lit - 1;
	return n_out;
}

static int emit(const char *path, u32 n)
{
	char tmp[256];
	u32 i, l;
	long fd;
	int ok;

	if (eq(path, "-"))
		return write_all(1, out, n);
	l = slen(path);
	if (l > 250)
		return 0;
	for (i = 0; i < l; i++)
		tmp[i] = path[i];
	tmp[l] = '.';
	tmp[l + 1] = 't';
	tmp[l + 2] = 'm';
	tmp[l + 3] = 'p';
	tmp[l + 4] = 0;
	fd = sc(SYS_OPEN, (long)tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644, 0, 0, 0);
	if (fd < 0) {
		put(2, "homtom-shot: cannot create output\n");
		return 0;
	}
	ok = write_all(fd, out, n);
	sc(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
	if (!ok)
		return 0;
	return sc(SYS_RENAME, (long)tmp, (long)path, 0, 0, 0, 0) == 0;
}

static void info(const struct geo *g)
{
	static const char *names[9] = { "LCDCON1", "LCDCON2", "LCDCON3", "LCDCON4", "LCDCON5",
					"LCDSADDR1", "LCDSADDR2", "LCDSADDR3", "TPAL" };
	u32 i;

	for (i = 0; i < 9; i++)
		hex(1, names[i], g->reg[i]);
	put(1, "\n");
	hex(1, "fb", g->phys);
	dec(1, "w", g->w);
	dec(1, "h", g->h);
	dec(1, "stride", g->stride);
	dec(1, "hwswp", g->hwswp);
	dec(1, "envid", !(g->flags & F_OFF));
	dec(1, "tpal_on", !!(g->flags & F_TPAL));
	dec(1, "fallback", !!(g->flags & F_FALLBACK));
	put(1, "\n");
}

static void usage(void)
{
	put(2, "usage: homtom-shot [-o FILE] [-f hts|raw|bmp] [-s 1|2|4] [-l MS [-t S]] [-i]\n");
}

int main_c(long *sp)
{
	int argc = (int)sp[0];
	char **argv = (char **)(sp + 1);
	const char *path = "-";
	int fmt = FMT_HTS, want_info = 0, i;
	u32 scale = 1, loop_ms = 0, limit_s = 300, seq = 0, elapsed = 0;
	struct geo g;

	for (i = 1; i < argc; i++) {
		const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : 0;

		if (eq(a, "-i")) {
			want_info = 1;
		} else if (eq(a, "-o") && v) {
			path = v;
			i++;
		} else if (eq(a, "-f") && v) {
			fmt = eq(v, "raw") ? FMT_RAW : eq(v, "bmp") ? FMT_BMP : eq(v, "hts") ? FMT_HTS : -1;
			i++;
		} else if (eq(a, "-s") && v) {
			scale = num(v);
			i++;
		} else if (eq(a, "-l") && v) {
			loop_ms = num(v);
			i++;
		} else if (eq(a, "-t") && v) {
			limit_s = num(v);
			i++;
		} else {
			usage();
			return 2;
		}
	}
	if (fmt < 0 || (scale != 1 && scale != 2 && scale != 4) || (loop_ms && eq(path, "-"))) {
		usage();
		return 2;
	}
	if (loop_ms && loop_ms < 100)
		loop_ms = 100;
	if (limit_s > 3600)
		limit_s = 3600;
	if (!probe(&g, want_info))
		return 1;
	if (want_info) {
		info(&g);
		return 0;
	}
	if (!map_fb(&g))
		return 1;
	for (;;) {
		u32 n = fmt == FMT_HTS ? encode_rle(&g, scale, seq) : fmt == FMT_RAW ? encode_raw(&g, scale, seq) : encode_bmp(&g, scale);

		if (!emit(path, n))
			return 1;
		if (!loop_ms)
			return 0;
		{
			long ts[2];

			ts[0] = 0;
			ts[1] = (long)loop_ms * 1000000L;
			while (ts[1] >= 1000000000L) {
				ts[0]++;
				ts[1] -= 1000000000L;
			}
			sc(SYS_NANOSLEEP, (long)ts, 0, 0, 0, 0, 0);
		}
		elapsed += loop_ms;
		if (elapsed >= limit_s * 1000)
			return 0;
		seq++;
		/* the scan-out address can move (e.g. a page flip); re-read it each frame */
		{
			struct geo ng;

			if (probe(&ng, 1)) {
				if (ng.phys != g.phys || ng.w != g.w || ng.h != g.h || ng.stride != g.stride) {
					sc(SYS_MUNMAP, (long)fbmap_base, fbmap_len, 0, 0, 0, 0);
					fbmap = 0;
					g = ng;
					if (!map_fb(&g))
						return 1;
				}
				g.flags = ng.flags;
				g.tpal = ng.tpal;
			}
		}
	}
}

__asm__(".globl _start\n_start:\n mov r0, sp\n bl main_c\n mov r7, #1\n svc 0\n");
