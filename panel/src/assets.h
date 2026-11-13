#ifndef ASSETS_H
#define ASSETS_H

/* 4-bit alpha, two pixels per byte (high nibble first), rows padded to a byte. */
struct glyph {
	unsigned short off;
	unsigned char w, h;
	signed char bx, by;	/* left bearing, top above the baseline */
	unsigned short adv;	/* advance in 1/16 px */
};

struct font {
	const struct glyph *glyphs;	/* code points 32..255 */
	const unsigned char *bits;
	int ascent, descent;
};

struct icon {
	int w, h;
	const unsigned char *bits;
};

extern const struct font font_small, font_label, font_value;
extern const struct icon icon_light, icon_switch, icon_climate, icon_sensor, icon_scene,
	icon_script, icon_fan, icon_cover, icon_lock, icon_generic, icon_mark;

#endif
