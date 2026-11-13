#include <string.h>
#include "ui.h"

#define INK RGB(0x0E, 0x16, 0x1E)
#define BONE RGB(0xF5, 0xF3, 0xEE)
#define BLUE RGB(0x18, 0xBC, 0xF2)
#define TILE RGB(0x18, 0x23, 0x2D)
#define TILE_ON RGB(0x12, 0x30, 0x42)
#define TILE_DOWN RGB(0x26, 0x34, 0x40)
#define DISC RGB(0x25, 0x32, 0x3D)
#define MUTED RGB(0x8C, 0x96, 0x9E)
#define DIM RGB(0x55, 0x5F, 0x68)
#define AMBER RGB(0xF2, 0xB1, 0x18)
#define RED RGB(0xE8, 0x5D, 0x4F)
#define RADIUS 12
#define DISC_R 19

static const struct icon *icons[] = {
	&icon_generic, &icon_light, &icon_switch, &icon_climate, &icon_sensor,
	&icon_scene, &icon_script, &icon_fan, &icon_cover, &icon_lock
};

struct entity *ui_slot_entity(const struct view *v, int slot)
{
	const struct page *pg;
	if (!v->loaded || v->page >= v->m->npages || slot < 0)
		return NULL;
	pg = &v->m->pages[v->page];
	return slot < pg->count ? &v->m->ent[pg->first + slot] : NULL;
}

static void icon_at(struct canvas *c, const struct icon *ic, int cx, int cy, unsigned short col)
{
	gfx_alpha(c, cx - ic->w / 2, cy - ic->h / 2, ic->w, ic->h, ic->bits, col);
}

static void text_centered(struct canvas *c, const struct font *f, int cx, int base, const char *s,
			  unsigned short col, int max_w)
{
	int w = gfx_text_width(f, s, 0);
	if (w > max_w)
		w = max_w;
	gfx_text(c, f, cx - w / 2, base, s, col, 0, max_w);
}

void ui_bar(struct canvas *c, const struct view *v)
{
	struct rect dots;
	const char *label;
	unsigned short dot;
	int i, w, x;

	gfx_fill(c, 0, 0, SCREEN_W, BAR_H, INK);
	icon_at(c, &icon_mark, 20, BAR_H / 2, BLUE);
	/* Wordmark tracking -0.045em of 17 px, in 1/16 px per glyph. */
	gfx_text(c, &font_label, 34, 23, "homtom", BONE, -12, 0);

	if (v->loaded && v->m->npages > 0) {
		const char *title = v->m->pages[v->page].title;
		text_centered(c, &font_small, SCREEN_W / 2, 17, title[0] ? title : v->m->title, BONE, 150);
		if (v->m->npages > 1) {
			layout_page_dots(v->m->npages, &dots);
			for (i = 0; i < v->m->npages; i++)
				gfx_circle(c, dots.x + 8 + i * 14 + 7, 26, 3, i == v->page ? BLUE : DIM);
		}
	}

	switch (v->link) {
	case LINK_LIVE: label = "live"; dot = BLUE; break;
	case LINK_OFFLINE: label = "offline"; dot = RED; break;
	default: label = "connecting"; dot = AMBER; break;
	}
	if (v->note[0])
		label = v->note;
	w = gfx_text_width(&font_small, label, 0);
	if (w > 140)
		w = 140;
	x = SCREEN_W - GAP - w;
	gfx_text(c, &font_small, x, 22, label, v->note[0] ? BONE : MUTED, 0, 140);
	gfx_circle(c, x - 9, 17, 4, dot);
}

static void climate_step(struct canvas *c, int cx, int cy, int plus, unsigned short fg)
{
	gfx_ring(c, cx, cy, 14, 2, DISC);
	gfx_fill(c, cx - 6, cy - 1, 12, 2, fg);
	if (plus)
		gfx_fill(c, cx - 1, cy - 6, 2, 12, fg);
}

void ui_tile(struct canvas *c, const struct view *v, int slot)
{
	struct rect r;
	struct entity *e = ui_slot_entity(v, slot);
	int on, cx, cy, inner;
	unsigned short bg, name_col, value_col;

	layout_tile(slot, &r);
	gfx_fill(c, r.x, r.y, r.w, r.h, INK);
	if (!e)
		return;
	on = e->on == '1';
	bg = v->pressed == slot ? TILE_DOWN : on ? TILE_ON : TILE;
	gfx_rrect(c, r.x, r.y, r.w, r.h, RADIUS, bg);

	name_col = e->on == 'u' ? MUTED : BONE;
	value_col = e->on == 'u' ? DIM : on ? BLUE : MUTED;
	cy = r.y + 14 + DISC_R;
	cx = e->action == 'c' ? r.x + r.w / 2 : r.x + 14 + DISC_R;
	gfx_circle(c, cx, cy, DISC_R, on ? BLUE : DISC);
	icon_at(c, icons[e->icon], cx, cy, on ? INK : e->on == 'u' ? DIM : BONE);
	if (e->action == 'c') {
		climate_step(c, r.x + 26, cy, 0, BONE);
		climate_step(c, r.x + r.w - 26, cy, 1, BONE);
	}
	if (e->pending)
		gfx_ring(c, r.x + r.w - 16, r.y + 16, 6, 2, BONE);

	inner = r.w - 28;
	gfx_text(c, &font_label, r.x + 14, r.y + r.h - 33, e->name, name_col, 0, inner);
	gfx_text(c, &font_value, r.x + 14, r.y + r.h - 13, e->value, value_col, 0, inner);
}

void ui_screen(struct canvas *c, const struct view *v)
{
	int i;
	ui_bar(c, v);
	gfx_fill(c, 0, BAR_H, SCREEN_W, SCREEN_H - BAR_H, INK);
	for (i = 0; i < PER_PAGE; i++)
		ui_tile(c, v, i);
}

void ui_message(struct canvas *c, const char *title, const char *detail)
{
	gfx_fill(c, 0, BAR_H, SCREEN_W, SCREEN_H - BAR_H, INK);
	icon_at(c, &icon_mark, SCREEN_W / 2, 118, BLUE);
	text_centered(c, &font_label, SCREEN_W / 2, 160, title, BONE, SCREEN_W - 40);
	text_centered(c, &font_small, SCREEN_W / 2, 184, detail, MUTED, SCREEN_W - 40);
}
