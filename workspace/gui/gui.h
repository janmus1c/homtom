/* gui: tiny freestanding drawing + widget library for the 480x272 RGB565 LCD. No libc, no libgcc, no memcpy/memset.
 * Everything draws into a caller-supplied RGB565 buffer (the RAM back buffer); the hardware layer blits it.
 * Look and tokens: docs/UI_DESIGN.md (Home Assistant colours, Roboto, Material Design Icons). */
#ifndef GUI_LIB_H
#define GUI_LIB_H

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef unsigned int u32;

#define GUI_W 480
#define GUI_H 272

#define RGB(r, g, b) ((u16)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

/* theme tokens (docs/UI_DESIGN.md section 5): widgets take roles, the theme table gives the RGB565 colour */
enum {
	T_BG, T_CARD, T_BORDER, T_CHIP_OFF, T_TEXT, T_TEXT2, T_DISABLED, T_DOT_OFF, T_PRIMARY, T_MARK, T_AMBER, T_HEAT,
	T_COOL, T_STEEL, T_GREEN, T_ERROR, T_WARNING, T_CHIP_AMBER, T_CHIP_HEAT, T_CHIP_COOL, T_CHIP_STEEL, T_CHIP_GREEN,
	T_CHIP_PRIMARY, T_LIGHT_FILL, T_PRESSED_AMBER, T_PRESSED, T_WHITE, T_BONE, T_N
};
extern const u16 *gui_tok;
void gui_theme(int light);		/* 0 dark (default), 1 light */
#define TK(role) (gui_tok[role])

/* older names, used by the homtom-gui dashboard (ui.c) */
#define C_INK TK(T_BG)
#define C_CARD TK(T_CARD)
#define C_LINE TK(T_BORDER)
#define C_BLUE TK(T_PRIMARY)
#define C_BONE TK(T_TEXT)
#define C_MUTED TK(T_TEXT2)
#define C_OK TK(T_GREEN)
#define C_WARN TK(T_WARNING)
#define C_BAD TK(T_ERROR)

/* fonts: 4-bit alpha glyph tables rendered from Roboto by gen_font.py. Glyphs 32..last directly, then `next` extra
   codepoints (sorted in `ext`). Text is UTF-8; a character the font lacks draws as '?' (or nothing in the 28 px font) */
typedef struct { u16 off; u8 w, h; s8 dx, dy; u8 adv; } gui_glyph;
typedef struct { const gui_glyph *g; const u8 *bits; const u16 *ext; u8 next, height, ascent, last; } gui_font;
extern const gui_font *const GUI_T12;	/* 12/400: state lines, secondary text */
extern const gui_font *const GUI_T14;	/* 14/500: names, values, button labels */
extern const gui_font *const GUI_T16;	/* 16/500: page title */
extern const gui_font *const GUI_T20;	/* 20/400: values of generic tiles */
extern const gui_font *const GUI_T28;	/* 28/400: big numbers; digits . , : % + - ° C K and space only */
#define GUI_FONT_S GUI_T12
#define GUI_FONT_M GUI_T14
#define GUI_FONT_L GUI_T28

/* target and clip. Pass the buffer once; all calls draw into it, clipped to the clip rectangle. */
void gui_begin(u16 *buf, int w, int h);
void gui_clip(int x, int y, int w, int h);
void gui_unclip(void);
void gui_clip_in(int x, int y, int w, int h, int *save);	/* intersect with the current clip; save[4] for ... */
void gui_clip_out(const int *save);				/* ... restoring it */

u32 gui_udiv(u32 n, u32 d);		/* software divide (loop), d != 0 */
u32 gui_isqrt(u32 n);
u16 gui_mix(u16 bg, u16 fg, int a16);	/* a16 0..16: 0 = bg, 16 = fg */

/* primitives */
void gui_clear(u16 c);
void gui_px(int x, int y, u16 c);
void gui_blend(int x, int y, u16 c, int a16);
void gui_hline(int x, int y, int w, u16 c);
void gui_vline(int x, int y, int h, u16 c);
void gui_rect(int x, int y, int w, int h, u16 c);
void gui_frame(int x, int y, int w, int h, u16 c);			/* 1 px outline */
void gui_line(int x0, int y0, int x1, int y1, u16 c);			/* Bresenham, 1 px */
void gui_circle(int cx, int cy, int r, u16 c);				/* anti-aliased fill */
void gui_ring(int cx, int cy, int r, int t, u16 c);			/* anti-aliased outline, thickness t */
void gui_rrect(int x, int y, int w, int h, int r, u16 c);		/* anti-aliased rounded rectangle */
void gui_rrect_line(int x, int y, int w, int h, int r, int t, u16 c);	/* rounded outline, thickness t */

/* sub-pixel shapes: coordinates in 1/8 pixel (GUI_F(3) = 3 px). */
#define GUI_F(px) ((px) * 8)
void gui_stroke_f(int x0, int y0, int x1, int y1, int w8, u16 c);	/* anti-aliased line with round caps, width w8/8 px */
void gui_tri_f(int x0, int y0, int x1, int y1, int x2, int y2, u16 c);

/* text. gui_text*: y is the top of the font line box; gui_tb*: y is the baseline. Return the x after the last glyph. */
int gui_text(const gui_font *f, int x, int y, const char *s, u16 c);
int gui_text_tr(const gui_font *f, int x, int y, const char *s, u16 c, int track);
int gui_text_w(const gui_font *f, const char *s);
void gui_text_c(const gui_font *f, int x, int y, int w, const char *s, u16 c);	/* centred in [x, x+w) */
void gui_text_r(const gui_font *f, int x, int y, int w, const char *s, u16 c);	/* right-aligned to x+w */
int gui_tb(const gui_font *f, int x, int base, const char *s, u16 c);
int gui_tfit(const gui_font *f, int x, int base, int maxw, const char *s, u16 c);	/* cut with "…" to maxw */
int gui_tr(const gui_font *f, int right, int base, const char *s, u16 c);	/* right-aligned to `right`; returns its x */
int gui_wordmark(int x, int base, u16 c);	/* "homtom", Poppins SemiBold 16 px (wordmark_data.h); returns the x after it */
extern const int gui_wordmark_xh;		/* its x-height in px */
extern const int gui_wordmark_adv;		/* pen advance over the whole word */

/* icons: Material Design Icons, 2 bpp, 20 or 24 px (icon_data.h, ICON_* ids). gui_icon_find: id by mdi name, -1 none */
void gui_icon(int id, int size, int x, int y, u16 c);
int gui_icon_find(const char *name);
void gui_chip(int cx, int cy, int d, u16 bg, int icon, int isz, u16 fg);	/* filled circle with a centred icon */

/* string helpers (return pointer to the terminating NUL so calls chain) */
char *gui_cat(char *d, const char *s);
char *gui_utoa(char *d, u32 v);
char *gui_ftoa(char *d, int v10);	/* fixed point, one decimal: 123 -> "12.3" */
char *gui_pad2(char *d, u32 v);		/* two digits, zero padded */

/* widgets */
#define GUI_PRESSED 1
#define GUI_ACTIVE 2
#define GUI_DANGER 4
#define GUI_OFF 8	/* disabled look */
void gui_mark(int x, int y, int size, u16 c);				/* homtom house + arrow, size px square */
void gui_card(int x, int y, int w, int h);				/* radius 12, card fill, 1 px border */
void gui_card_c(int x, int y, int w, int h, u16 fill);			/* the same with another fill (pressed) */
void gui_progress(int x, int y, int w, int h, int pct, u16 c);
void gui_battery(int x, int y, int w, int h, int pct, int charging);	/* pct < 0: unknown */
int gui_batt_ind(int right, int base, int pct, int charging);	/* header battery: outline, fill, bolt, "55%"; returns its x */
void gui_button(int x, int y, int w, int h, const char *label, int flags);	/* text button, radius 10 */
void gui_ctl_slider(int x, int y, int w, int h, int pct, u16 fill);	/* HA control slider: track, fill, handle */
void gui_ct_track(int x, int y, int w, int h, int k_lo, int k_hi);	/* colour temperature gradient, dithered */
void gui_ct_rgb(int k, u8 *rgb);					/* colour of a kelvin value on that track */
void gui_handle(int x, int y, int h);					/* white 4 px handle with a 1 px outline */
void gui_switch(int x, int y, int on, int flags);			/* 44x24 */
void gui_slider(int x, int y, int w, int h, int pct, int flags);	/* gui_ctl_slider in the primary colour */
void gui_dot(int cx, int cy, int r, u16 c);
int gui_hit(int x, int y, int w, int h, int px, int py);
int gui_slider_pct(int x, int w, int h, int px);			/* touch x on a track at x, w wide -> 0..100 */

#endif
