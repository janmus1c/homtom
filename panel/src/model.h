#ifndef MODEL_H
#define MODEL_H

#define MAX_ENT 48
#define MAX_PAGES 8
#define PER_PAGE 6
#define ID_LEN 64
#define TEXT_LEN 32

enum { IC_GENERIC, IC_LIGHT, IC_SWITCH, IC_CLIMATE, IC_SENSOR, IC_SCENE, IC_SCRIPT, IC_FAN, IC_COVER, IC_LOCK };

struct entity {
	char id[ID_LEN];
	int icon;
	char action;		/* 't' toggle, 'p' press, 'c' climate, '-' read only */
	char on;		/* '1', '0' or 'u' (unavailable) */
	char value[TEXT_LEN];
	char name[TEXT_LEN];
	int pending;		/* actions in flight */
	int dirty;
};

struct page {
	char title[TEXT_LEN];
	int first, count;
};

struct model {
	char title[TEXT_LEN];
	unsigned long seq;
	int npages;
	struct page pages[MAX_PAGES];
	int nent;
	struct entity ent[MAX_ENT];
};

enum { PROTO_ERROR = -1, PROTO_OK = 0, PROTO_RELOAD = 1 };

/* Both parsers tokenise buf in place. */
int proto_parse_panel(struct model *m, char *buf, int len);
int proto_apply(struct model *m, char *buf, int len);
int proto_find(const struct model *m, const char *id);
int proto_valid_id(const char *id);

#endif
