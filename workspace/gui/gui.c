/* gui: freestanding drawing + widget library, see gui.h. Hardware independent: builds for ARMv4T bare and for macOS. */
#include "gui.h"
#include "font_data.h"
#include "icon_data.h"
#include "wordmark_data.h"

const gui_font *const GUI_T12 = &gui_t12;
const gui_font *const GUI_T14 = &gui_t14;
const gui_font *const GUI_T16 = &gui_t16;
const gui_font *const GUI_T20 = &gui_t20;
const gui_font *const GUI_T28 = &gui_t28;

/* RGB565 tokens, docs/UI_DESIGN.md section 5; the chips not listed there are the state colour at 20 % over the card,
   mixed in 24-bit sRGB and rounded to RGB565 the same way */
static const u16 tok_dark[T_N] = {
	0x1082, 0x2104, 0x3186, 0x2945, 0xDEFB, 0x9CF3, 0x6B6D, 0x4A49, 0x04D8, 0x1DDD, 0xFE01, 0xFB64,
	0x24BE, 0x4393, 0x4D6A, 0xDA27, 0xFD20, 0x4A03, 0x4984, 0x21C9, 0x2987, 0x29E5,
	0x19C8, 0x3984, 0x39A4, 0x3186, 0xFFFF, 0xF79D,
};
static const u16 tok_light[T_N] = {
	0xF7BE, 0xFFFF, 0xDEFB, 0xEF7D, 0x10A2, 0x5AEB, 0xBDF7, 0xBDF7, 0x04D8, 0x04D8, 0xFE01, 0xFB64,
	0x24BE, 0x4393, 0x4D6A, 0xDA27, 0xFD20, 0xFF99, 0xFF1A, 0xD75F, 0xDF1D, 0xDF7B,
	0xCF5E, 0xFFBB, 0xFF99, 0xEF5D, 0xFFFF, 0x10A2,
};
const u16 *gui_tok = tok_dark;

void gui_theme(int light)
{
	gui_tok = light ? tok_light : tok_dark;
}

static u16 *fb;
static int fw, fh;
static int cx0, cy0, cx1, cy1;	/* clip, half open */

void gui_begin(u16 *buf, int w, int h)
{
	fb = buf;
	fw = w;
	fh = h;
	gui_unclip();
}

void gui_unclip(void)
{
	cx0 = 0;
	cy0 = 0;
	cx1 = fw;
	cy1 = fh;
}

void gui_clip_in(int x, int y, int w, int h, int *save)
{
	save[0] = cx0;
	save[1] = cy0;
	save[2] = cx1;
	save[3] = cy1;
	if (x > cx0)
		cx0 = x;
	if (y > cy0)
		cy0 = y;
	if (x + w < cx1)
		cx1 = x + w;
	if (y + h < cy1)
		cy1 = y + h;
}

void gui_clip_out(const int *save)
{
	cx0 = save[0];
	cy0 = save[1];
	cx1 = save[2];
	cy1 = save[3];
}

void gui_clip(int x, int y, int w, int h)
{
	cx0 = x < 0 ? 0 : x;
	cy0 = y < 0 ? 0 : y;
	cx1 = x + w > fw ? fw : x + w;
	cy1 = y + h > fh ? fh : y + h;
}

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int iabs(int a) { return a < 0 ? -a : a; }
static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

u32 gui_udiv(u32 n, u32 d)
{
	u32 q = 0, r = 0;
	int i;

	for (i = 31; i >= 0; i--) {
		r = (r << 1) | ((n >> i) & 1);
		if (r >= d) {
			r -= d;
			q |= 1u << i;
		}
	}
	return q;
}

u32 gui_isqrt(u32 n)
{
	u32 r = 0, bit = 1u << 30;

	while (bit > n)
		bit >>= 2;
	while (bit) {
		if (n >= r + bit) {
			n -= r + bit;
			r = (r >> 1) + bit;
		} else {
			r >>= 1;
		}
		bit >>= 2;
	}
	return r;
}

u16 gui_mix(u16 bg, u16 fg, int a)
{
	u32 b = (bg | (u32)bg << 16) & 0x07E0F81Fu;
	u32 f = (fg | (u32)fg << 16) & 0x07E0F81Fu;
	u32 m = (b * (16 - a) + f * a) >> 4;

	m &= 0x07E0F81Fu;
	return (u16)(m | m >> 16);
}

void gui_clear(u16 c)
{
	int i, n = fw * fh;

	for (i = 0; i < n; i++)
		fb[i] = c;
}

void gui_px(int x, int y, u16 c)
{
	if (x >= cx0 && x < cx1 && y >= cy0 && y < cy1)
		fb[y * fw + x] = c;
}

void gui_blend(int x, int y, u16 c, int a)
{
	u16 *p;

	if (a <= 0 || x < cx0 || x >= cx1 || y < cy0 || y >= cy1)
		return;
	p = &fb[y * fw + x];
	*p = a >= 16 ? c : gui_mix(*p, c, a);
}

void gui_hline(int x, int y, int w, u16 c)
{
	int x1 = imin(x + w, cx1), i;

	if (y < cy0 || y >= cy1)
		return;
	for (i = imax(x, cx0); i < x1; i++)
		fb[y * fw + i] = c;
}

void gui_vline(int x, int y, int h, u16 c)
{
	int y1 = imin(y + h, cy1), j;

	if (x < cx0 || x >= cx1)
		return;
	for (j = imax(y, cy0); j < y1; j++)
		fb[j * fw + x] = c;
}

void gui_rect(int x, int y, int w, int h, u16 c)
{
	int j;

	for (j = 0; j < h; j++)
		gui_hline(x, y + j, w, c);
}

void gui_frame(int x, int y, int w, int h, u16 c)
{
	gui_hline(x, y, w, c);
	gui_hline(x, y + h - 1, w, c);
	gui_vline(x, y, h, c);
	gui_vline(x + w - 1, y, h, c);
}

void gui_line(int x0, int y0, int x1, int y1, u16 c)
{
	int dx = iabs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = -iabs(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int err = dx + dy, e2;

	for (;;) {
		gui_px(x0, y0, c);
		if (x0 == x1 && y0 == y1)
			break;
		e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
}

/* signed distance from pixel (px,py) to the rounded rectangle edge, in 1/16 px (negative inside) */
static int rr_sdf16(int px, int py, int x, int y, int w, int h, int r)
{
	int qx = iabs(2 * px + 1 - (2 * x + w)) - (w - 2 * r);
	int qy = iabs(2 * py + 1 - (2 * y + h)) - (h - 2 * r);
	int d;

	if (qx > 0 && qy > 0)
		d = (int)gui_isqrt((u32)(qx * qx + qy * qy) * 64);
	else
		d = imax(qx, qy) * 8;
	return d - 16 * r;
}

static int rr_cov(int px, int py, int x, int y, int w, int h, int r)
{
	return clamp(8 - rr_sdf16(px, py, x, y, w, h, r), 0, 16);
}

void gui_rrect(int x, int y, int w, int h, int r, u16 c)
{
	int j, i;

	r = clamp(r, 0, imin(w, h) / 2);
	for (j = y; j < y + h; j++) {
		if (j >= y + r && j < y + h - r) {
			gui_hline(x, j, w, c);
			continue;
		}
		for (i = x; i < x + r; i++)
			gui_blend(i, j, c, rr_cov(i, j, x, y, w, h, r));
		for (i = x + w - r; i < x + w; i++)
			gui_blend(i, j, c, rr_cov(i, j, x, y, w, h, r));
		gui_hline(x + r, j, w - 2 * r, c);
	}
}

void gui_rrect_line(int x, int y, int w, int h, int r, int t, u16 c)
{
	int j, i, band, ri;

	r = clamp(r, 0, imin(w, h) / 2);
	ri = imax(r - t, 0);
	band = imax(r, t);
	for (j = y; j < y + h; j++) {
		if (j >= y + band && j < y + h - band) {
			gui_hline(x, j, t, c);
			gui_hline(x + w - t, j, t, c);
			continue;
		}
		for (i = x; i < x + w; i++) {
			int a = rr_cov(i, j, x, y, w, h, r) - rr_cov(i, j, x + t, y + t, w - 2 * t, h - 2 * t, ri);

			gui_blend(i, j, c, a);
		}
	}
}

void gui_circle(int cx, int cy, int r, u16 c)
{
	gui_rrect(cx - r, cy - r, 2 * r, 2 * r, r, c);
}

void gui_ring(int cx, int cy, int r, int t, u16 c)
{
	gui_rrect_line(cx - r, cy - r, 2 * r, 2 * r, r, t, c);
}

void gui_stroke_f(int x0, int y0, int x1, int y1, int w8, u16 c)
{
	int vx = x1 - x0, vy = y1 - y0;
	int l2 = vx * vx + vy * vy;
	int len = (int)gui_isqrt((u32)l2);
	int pad = w8 / 2 + 8;
	int bx0 = (imin(x0, x1) - pad) >> 3, bx1 = ((imax(x0, x1) + pad) >> 3) + 1;
	int by0 = (imin(y0, y1) - pad) >> 3, by1 = ((imax(y0, y1) + pad) >> 3) + 1;
	int px, py;

	bx0 = imax(bx0, cx0);
	by0 = imax(by0, cy0);
	bx1 = imin(bx1, cx1);
	by1 = imin(by1, cy1);
	for (py = by0; py < by1; py++) {
		for (px = bx0; px < bx1; px++) {
			int dx = px * 8 + 4 - x0, dy = py * 8 + 4 - y0;
			int dot = dx * vx + dy * vy, d16;

			if (l2 == 0 || dot <= 0) {
				d16 = (int)gui_isqrt((u32)(dx * dx + dy * dy) * 4);
			} else if (dot >= l2) {
				int ex = dx - vx, ey = dy - vy;

				d16 = (int)gui_isqrt((u32)(ex * ex + ey * ey) * 4);
			} else {
				d16 = (int)gui_udiv((u32)iabs(dx * vy - dy * vx) * 2, (u32)len);
			}
			gui_blend(px, py, c, clamp(8 + w8 - d16, 0, 16));
		}
	}
}

void gui_tri_f(int x0, int y0, int x1, int y1, int x2, int y2, u16 c)
{
	int bx0 = imax(imin(x0, imin(x1, x2)) >> 3, cx0), bx1 = imin((imax(x0, imax(x1, x2)) >> 3) + 1, cx1);
	int by0 = imax(imin(y0, imin(y1, y2)) >> 3, cy0), by1 = imin((imax(y0, imax(y1, y2)) >> 3) + 1, cy1);
	int px, py;

	for (py = by0; py < by1; py++) {
		for (px = bx0; px < bx1; px++) {
			int X = px * 8 + 4, Y = py * 8 + 4;
			int e0 = (x1 - x0) * (Y - y0) - (y1 - y0) * (X - x0);
			int e1 = (x2 - x1) * (Y - y1) - (y2 - y1) * (X - x1);
			int e2 = (x0 - x2) * (Y - y2) - (y0 - y2) * (X - x2);

			if ((e0 >= 0 && e1 >= 0 && e2 >= 0) || (e0 <= 0 && e1 <= 0 && e2 <= 0))
				gui_px(px, py, c);
		}
	}
}


/* ---------- text ---------- */

/* next UTF-8 codepoint (a broken sequence yields '?' and skips one byte) */
static int utf8(const char **ps)
{
	const u8 *s = (const u8 *)*ps;
	int c = s[0], n = 0, i;

	if (c < 0x80)
		n = 0;
	else if ((c & 0xE0) == 0xC0)
		n = 1, c &= 0x1F;
	else if ((c & 0xF0) == 0xE0)
		n = 2, c &= 0x0F;
	else
		n = -1;
	for (i = 1; i <= n; i++) {
		if ((s[i] & 0xC0) != 0x80) {
			n = -1;
			break;
		}
		c = c << 6 | (s[i] & 0x3F);
	}
	if (n < 0) {
		*ps += 1;
		return '?';
	}
	*ps += n + 1;
	return c;
}

static const gui_glyph *glyph(const gui_font *f, int c)
{
	int lo = 0, hi = f->next - 1;

	if (c >= 32 && c <= f->last)
		return &f->g[c - 32];
	while (lo <= hi) {
		int m = (lo + hi) >> 1;

		if (f->ext[m] == c)
			return &f->g[f->last - 31 + m];
		if (f->ext[m] < c)
			lo = m + 1;
		else
			hi = m - 1;
	}
	return &f->g[(f->last < '?' ? ' ' : '?') - 32];
}

static int nibble(const u8 *b, int k)
{
	return (k & 1) ? (b[k >> 1] & 15) : (b[k >> 1] >> 4);
}

/* bitmaps are run-length coded (gen_font.py pack): 1..14 one pixel; 0 or 15, then n: n + 1 pixels of that alpha.
   Transparent runs are skipped, opaque ones stored */
static void draw_glyph(const u8 *bits, const gui_glyph *g, int x, int y, u16 c)
{
	const u8 *b = bits + g->off;
	int i = 0, j = 0, k = 0, a, run;

	x += g->dx;
	y += g->dy;
	while (j < g->h) {
		a = nibble(b, k++);
		run = a == 0 || a == 15 ? nibble(b, k++) + 1 : 1;
		for (; run > 0; run--) {
			if (a)
				gui_blend(x + i, y + j, c, a + (a >> 3));
			if (++i == g->w) {
				i = 0;
				j++;
			}
		}
	}
}

int gui_text_tr(const gui_font *f, int x, int y, const char *s, u16 c, int track)
{
	while (*s) {
		const gui_glyph *g = glyph(f, utf8(&s));

		draw_glyph(f->bits, g, x, y, c);
		x += g->adv + track;
	}
	return x;
}

int gui_text(const gui_font *f, int x, int y, const char *s, u16 c)
{
	return gui_text_tr(f, x, y, s, c, 0);
}

int gui_text_w(const gui_font *f, const char *s)
{
	int w = 0;

	while (*s)
		w += glyph(f, utf8(&s))->adv;
	return w;
}

void gui_text_c(const gui_font *f, int x, int y, int w, const char *s, u16 c)
{
	gui_text(f, x + (w - gui_text_w(f, s)) / 2, y, s, c);
}

void gui_text_r(const gui_font *f, int x, int y, int w, const char *s, u16 c)
{
	gui_text(f, x + w - gui_text_w(f, s), y, s, c);
}

int gui_tb(const gui_font *f, int x, int base, const char *s, u16 c)
{
	return gui_text(f, x, base - f->ascent, s, c);
}

int gui_tr(const gui_font *f, int right, int base, const char *s, u16 c)
{
	int x = right - gui_text_w(f, s);

	gui_tb(f, x, base, s, c);
	return x;
}

int gui_tfit(const gui_font *f, int x, int base, int maxw, const char *s, u16 c)
{
	int w = 0, ew = glyph(f, 0x2026)->adv, y = base - f->ascent;
	const char *p = s;

	if (gui_text_w(f, s) <= maxw)
		return gui_text(f, x, y, s, c);
	while (*p) {
		const gui_glyph *g = glyph(f, utf8(&p));

		if (w + g->adv + ew > maxw)
			break;
		draw_glyph(f->bits, g, x + w, y, c);
		w += g->adv;
	}
	draw_glyph(f->bits, glyph(f, 0x2026), x + w, y, c);
	return x + w + ew;
}

const int gui_wordmark_xh = GUI_WORDMARK_XH;
const int gui_wordmark_adv = GUI_WORDMARK_ADV;

int gui_wordmark(int x, int base, u16 c)
{
	draw_glyph(gui_wordmark_bits, &gui_wordmark_glyph, x, base, c);
	return x + gui_wordmark_glyph.adv;
}

/* ---------- icons ---------- */

int gui_icon_find(const char *name)
{
	int i;

	for (i = 0; i < GUI_ICON_N; i++) {
		const char *a = gui_icon_name[i], *b = name;

		while (*a && *a == *b)
			a++, b++;
		if (!*a && !*b)
			return i;
	}
	return -1;
}

/* 2 bpp: 0 skip, 1 and 2 blend at 5/16 and 11/16, 3 store */
void gui_icon(int id, int size, int x, int y, u16 c)
{
	static const u8 a16[4] = { 0, 5, 11, 16 };
	const u8 *b;
	int k = size >= 24 ? 1 : 0, s, i, j, rb;

	if (id < 0 || id >= GUI_ICON_N)
		return;
	s = gui_icon_size[k];
	rb = (s + 3) >> 2;
	b = gui_icon_bits + gui_icon_off[id][k];
	x += (size - s) / 2;
	y += (size - s) / 2;
	for (j = 0; j < s; j++, b += rb)
		for (i = 0; i < s; i++) {
			int v = (b[i >> 2] >> (6 - 2 * (i & 3))) & 3;

			if (v)
				gui_blend(x + i, y + j, c, a16[v]);
		}
}

void gui_chip(int cx, int cy, int d, u16 bg, int icon, int isz, u16 fg)
{
	gui_rrect(cx - d / 2, cy - d / 2, d, d, d / 2, bg);
	gui_icon(icon, isz, cx - isz / 2, cy - isz / 2, fg);
}

/* ---------- strings ---------- */

char *gui_cat(char *d, const char *s)
{
	while (*s)
		*d++ = *s++;
	*d = 0;
	return d;
}

char *gui_utoa(char *d, u32 v)
{
	char t[11];
	int k = 0;

	do {
		u32 q = gui_udiv(v, 10);

		t[k++] = (char)('0' + (v - q * 10));
		v = q;
	} while (v);
	while (k)
		*d++ = t[--k];
	*d = 0;
	return d;
}

char *gui_ftoa(char *d, int v10)
{
	if (v10 < 0) {
		*d++ = '-';
		v10 = -v10;
	}
	d = gui_utoa(d, (u32)v10 / 10);
	*d++ = '.';
	*d++ = (char)('0' + (u32)v10 % 10);
	*d = 0;
	return d;
}

char *gui_pad2(char *d, u32 v)
{
	d[0] = (char)('0' + (v / 10) % 10);
	d[1] = (char)('0' + v % 10);
	d[2] = 0;
	return d + 2;
}

/* ---------- widgets ---------- */

int gui_hit(int x, int y, int w, int h, int px, int py)
{
	return px >= x && px < x + w && py >= y && py < y + h;
}

void gui_dot(int cx, int cy, int r, u16 c)
{
	gui_circle(cx, cy, r, c);
}

void gui_mark(int x, int y, int size, u16 c)
{
	static const u8 house[5][2] = { { 14, 56 }, { 60, 16 }, { 106, 56 }, { 106, 104 }, { 14, 104 } };
	static const u8 arrow[4][2] = { { 60, 54 }, { 72, 84 }, { 60, 77 }, { 48, 84 } };
	int hx[5], hy[5], ax[4], ay[4], i, w1 = size * 3 / 5, w2 = size * 2 / 5;

	for (i = 0; i < 5; i++) {
		hx[i] = x * 8 + house[i][0] * size / 15;
		hy[i] = y * 8 + house[i][1] * size / 15;
	}
	for (i = 0; i < 4; i++) {
		ax[i] = x * 8 + arrow[i][0] * size / 15;
		ay[i] = y * 8 + arrow[i][1] * size / 15;
	}
	for (i = 0; i < 5; i++)
		gui_stroke_f(hx[i], hy[i], hx[(i + 1) % 5], hy[(i + 1) % 5], w1, c);
	gui_tri_f(ax[0], ay[0], ax[1], ay[1], ax[2], ay[2], c);
	gui_tri_f(ax[0], ay[0], ax[2], ay[2], ax[3], ay[3], c);
	for (i = 0; i < 4; i++)
		gui_stroke_f(ax[i], ay[i], ax[(i + 1) % 4], ay[(i + 1) % 4], w2, c);
}

void gui_card_c(int x, int y, int w, int h, u16 fill)
{
	gui_rrect(x, y, w, h, 12, TK(T_BORDER));
	gui_rrect(x + 1, y + 1, w - 2, h - 2, 11, fill);
}

void gui_card(int x, int y, int w, int h)
{
	gui_card_c(x, y, w, h, TK(T_CARD));
}

void gui_progress(int x, int y, int w, int h, int pct, u16 c)
{
	int fillw;

	gui_rrect(x, y, w, h, h / 2, TK(T_CHIP_OFF));
	pct = clamp(pct, 0, 100);
	if (!pct)
		return;
	fillw = imax(w * pct / 100, h);
	gui_rrect(x, y, fillw, h, h / 2, c);
}

void gui_battery(int x, int y, int w, int h, int pct, int charging)
{
	int bw = w - 5, iw = bw - 8, fillw;
	u16 fg = TK(T_TEXT2), col = pct > 15 ? TK(T_STEEL) : TK(T_ERROR);

	gui_rrect_line(x, y, bw, h, 5, 2, fg);
	gui_rrect(x + bw + 1, y + h / 2 - h / 6, 4, h / 3, 2, fg);
	if (pct >= 0) {
		fillw = iw * clamp(pct, 0, 100) / 100;
		if (fillw > 0)
			gui_rrect(x + 4, y + 4, fillw < 4 ? 4 : fillw, h - 8, 2, col);
	}
	if (charging)
		gui_icon(ICON_BATTERY_CHARGING, 20, x + bw / 2 - 10, y + h / 2 - 10, TK(T_TEXT));
}

/* header battery (user request): 22x11 outline with a 2x5 nub, fill by level (green from 40 %, amber from 15 %, red
   below), a bolt over the body while charging, the percentage after it; "--%" and a grey outline when unknown */
int gui_batt_ind(int right, int base, int pct, int charging)
{
	char b[8];
	int x, y = base - 10, fw;
	u16 ol = TK(T_TEXT2), fill = pct >= 40 ? TK(T_GREEN) : pct >= 15 ? TK(T_AMBER) : TK(T_ERROR);

	if (pct < 0)
		gui_cat(b, "--%");
	else
		gui_cat(gui_utoa(b, (u32)clamp(pct, 0, 100)), "%");
	x = gui_tr(GUI_T12, right, base, b, pct < 0 ? TK(T_DISABLED) : TK(T_TEXT2)) - 4 - 24;
	if (pct < 0)
		ol = TK(T_DISABLED);
	gui_rrect_line(x, y, 22, 11, 3, 1, ol);
	gui_rect(x + 22, y + 3, 2, 5, ol);
	fw = pct < 0 ? 0 : (18 * clamp(pct, 0, 100) + 50) / 100;
	if (fw > 0)
		gui_rrect(x + 2, y + 2, fw, 7, 1, fill);
	if (charging) {
		/* bolt, 7x11 px, centred on the body: two triangles in 1/8 px around the centre, first in the page colour
		   shifted by one pixel each way (an outline that keeps it readable on the fill), then in white */
		static const s8 bolt[2][6] = { { 20, -44, -28, 8, 12, 8 }, { -12, -8, 28, -8, -20, 44 } };
		static const s8 sh[5][2] = { { -8, 0 }, { 8, 0 }, { 0, -8 }, { 0, 8 }, { 0, 0 } };
		int i, k, cx = x * 8 + 88, cy = y * 8 + 44;

		for (k = 0; k < 5; k++)
			for (i = 0; i < 2; i++) {
				const s8 *t = bolt[i];
				int dx = cx + sh[k][0], dy = cy + sh[k][1];

				gui_tri_f(dx + t[0], dy + t[1], dx + t[2], dy + t[3], dx + t[4], dy + t[5],
					  k < 4 ? TK(T_BG) : TK(T_WHITE));
			}
	}
	return x;
}

void gui_button(int x, int y, int w, int h, const char *label, int flags)
{
	u16 bg = TK(T_CHIP_OFF), fg = TK(T_PRIMARY);

	if (flags & GUI_ACTIVE)
		bg = TK(T_CHIP_PRIMARY);
	if (flags & GUI_DANGER)
		fg = TK(T_ERROR);
	if (flags & GUI_OFF)
		fg = TK(T_DISABLED);
	if (flags & GUI_PRESSED)
		bg = TK(T_PRESSED);
	gui_rrect(x, y, w, h, 10, bg);
	gui_tb(GUI_T14, x + (w - gui_text_w(GUI_T14, label)) / 2, y + h / 2 + 5, label, fg);
}

void gui_handle(int x, int y, int h)
{
	gui_rrect(x - 1, y - 1, 6, h + 2, 3, TK(T_BG));
	gui_rrect(x, y, 4, h, 2, TK(T_WHITE));
}

/* HA control slider: track in chip_off, fill from the left, white 4 px handle inset by h/8 from the fill end */
void gui_ctl_slider(int x, int y, int w, int h, int pct, u16 fill)
{
	int fw = w * clamp(pct, 0, 100) / 100, in = h / 8;

	gui_rrect(x, y, w, h, 8, TK(T_CHIP_OFF));
	if (fw > 0) {
		int sv[4];

		gui_clip_in(x, y, fw, h, sv);
		gui_rrect(x, y, w, h, 8, fill);
		gui_clip_out(sv);
	}
	gui_rrect(imax(x + fw - in - 4, x + in), y + in, 4, h - 2 * in, 2, TK(T_WHITE));
}

/* 2000 K .. 6500 K, stops at 0, 15, 33, 55, 75, 100 % */
static const u8 ct_stop[6][4] = {
	{ 0, 0xFF, 0x8B, 0x14 }, { 15, 0xFF, 0xA9, 0x57 }, { 33, 0xFF, 0xC4, 0x89 },
	{ 55, 0xFF, 0xDD, 0xB4 }, { 75, 0xFF, 0xED, 0xDC }, { 100, 0xF3, 0xF2, 0xFF },
};

void gui_ct_rgb(int k, u8 *rgb)
{
	int p = clamp(k - 2000, 0, 4500) * 1000 / 4500, i = 0, c;

	while (i < 4 && p > ct_stop[i + 1][0] * 10)
		i++;
	for (c = 0; c < 3; c++) {
		int a = ct_stop[i][c + 1], b = ct_stop[i + 1][c + 1], s0 = ct_stop[i][0] * 10, s1 = ct_stop[i + 1][0] * 10;

		rgb[c] = (u8)(a + (b - a) * (int)gui_udiv((u32)(p - s0) * 256, (u32)(s1 - s0)) / 256);
	}
}

/* gradient rectangle with a 4x4 Bayer threshold before truncating to RGB565, rounded ends (radius 8) */
void gui_ct_track(int x, int y, int w, int h, int k_lo, int k_hi)
{
	static const u8 bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
	int i, j;

	for (i = 0; i < w; i++) {
		u8 rgb[3];
		int a16 = 16, px = x + i;

		gui_ct_rgb(k_lo + (int)gui_udiv((u32)i * (u32)(k_hi - k_lo), (u32)(w > 1 ? w - 1 : 1)), rgb);
		for (j = 0; j < h; j++) {
			int t = 2 * bayer[(y + j) & 3][px & 3] + 1;
			u32 r = ((u32)rgb[0] * 31 * 32 + (u32)t * 255) / 8160, g = ((u32)rgb[1] * 63 * 32 + (u32)t * 255) / 8160;
			u32 b = ((u32)rgb[2] * 31 * 32 + (u32)t * 255) / 8160;
			u16 c = (u16)((r > 31 ? 31 : r) << 11 | (g > 63 ? 63 : g) << 5 | (b > 31 ? 31 : b));

			if (i < 8 || i >= w - 8)
				a16 = (j < 8 || j >= h - 8) ? clamp(8 - rr_sdf16(px, y + j, x, y, w, h, 8), 0, 16) : 16;
			gui_blend(px, y + j, c, a16);
		}
	}
}

void gui_switch(int x, int y, int on, int flags)
{
	u16 t = on ? TK(T_PRIMARY) : TK(T_DOT_OFF), k = on ? TK(T_WHITE) : TK(T_TEXT2);

	if (flags & GUI_OFF)
		t = TK(T_DOT_OFF), k = TK(T_DISABLED);
	gui_rrect(x, y, 44, 24, 12, t);
	gui_circle(on ? x + 32 : x + 12, y + 12, 9, k);
}

void gui_slider(int x, int y, int w, int h, int pct, int flags)
{
	gui_ctl_slider(x, y, w, h, pct, (flags & GUI_OFF) ? TK(T_DISABLED) : TK(T_PRIMARY));
}

int gui_slider_pct(int x, int w, int h, int px)
{
	int v = clamp(px - x, 0, w);

	(void)h;
	return w > 0 ? (int)gui_udiv((u32)v * 100, (u32)w) : 0;
}
