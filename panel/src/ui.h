#ifndef UI_H
#define UI_H

#include "gfx.h"
#include "layout.h"
#include "model.h"

enum { LINK_WAIT, LINK_LIVE, LINK_OFFLINE };

struct view {
	struct model *m;
	int loaded;
	int page;
	int link;
	int pressed;		/* slot held down, or -1 */
	char note[48];		/* replaces the link label while set */
	char gateway[40];
};

void ui_bar(struct canvas *c, const struct view *v);
void ui_tile(struct canvas *c, const struct view *v, int slot);
void ui_screen(struct canvas *c, const struct view *v);
void ui_message(struct canvas *c, const char *title, const char *detail);
struct entity *ui_slot_entity(const struct view *v, int slot);

#endif
