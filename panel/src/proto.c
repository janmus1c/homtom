/* The gateway's line format: one record per line, fields separated by tabs.
 *   H 1 <seq> [title]                                  header, always first
 *   P <title>                                          starts a page
 *   E <entity> <icon> <action> <on> <value> <name>     a tile on the current page
 *   S <entity> <on> <value>                            state update
 *   R                                                  layout changed, fetch /v1/panel again
 */
#include <string.h>
#include <stdlib.h>
#include "model.h"

#define MAX_FIELDS 8

static const char *icon_names[] = {
	"generic", "light", "switch", "climate", "sensor", "scene", "script", "fan", "cover", "lock"
};

static void copy(char *dst, const char *src, int size)
{
	strncpy(dst, src, size - 1);
	dst[size - 1] = 0;
	/* Never leave half a UTF-8 sequence at the end. */
	if (strlen(src) >= (size_t)size - 1) {
		int n = strlen(dst);
		while (n > 0 && ((unsigned char)dst[n - 1] & 0xC0) == 0x80)
			n--;
		if (n > 0 && ((unsigned char)dst[n - 1] & 0xC0) == 0xC0)
			n--;
		dst[n] = 0;
	}
}

/* Next line as fields; returns the field count, 0 at the end of the buffer. */
static int next_line(char **pos, char *end, char **f)
{
	char *p = *pos, *line_end;
	int n = 0;

	while (p < end && (*p == '\n' || *p == '\r'))
		p++;
	if (p >= end)
		return 0;
	line_end = p;
	while (line_end < end && *line_end != '\n')
		line_end++;
	*pos = line_end < end ? line_end + 1 : end;
	if (line_end > p && line_end[-1] == '\r')
		line_end--;
	*line_end = 0;
	f[n++] = p;
	for (; *p; p++)
		if (*p == '\t' && n < MAX_FIELDS) {
			*p = 0;
			f[n++] = p + 1;
		}
	return n;
}

int proto_valid_id(const char *id)
{
	int dot = 0, n = 0;
	for (; *id; id++, n++) {
		if (*id == '.')
			dot++;
		else if (!((*id >= 'a' && *id <= 'z') || (*id >= '0' && *id <= '9') || *id == '_'))
			return 0;
	}
	return dot == 1 && n > 2 && n < ID_LEN;
}

static int parse_icon(const char *s)
{
	unsigned i;
	for (i = 0; i < sizeof icon_names / sizeof icon_names[0]; i++)
		if (!strcmp(s, icon_names[i]))
			return i;
	return IC_GENERIC;
}

static int parse_on(const char *s)
{
	return (s[0] == '1' || s[0] == '0') && !s[1] ? s[0] : 'u';
}

static int header(struct model *m, char **f, int n, int with_title)
{
	char *end;
	unsigned long seq;
	if (n < 3 || strcmp(f[0], "H") || strcmp(f[1], "1"))
		return PROTO_ERROR;
	seq = strtoul(f[2], &end, 10);
	if (*end || end == f[2])
		return PROTO_ERROR;
	m->seq = seq;
	if (with_title)
		copy(m->title, n > 3 ? f[3] : "", TEXT_LEN);
	return PROTO_OK;
}

int proto_find(const struct model *m, const char *id)
{
	int i;
	for (i = 0; i < m->nent; i++)
		if (!strcmp(m->ent[i].id, id))
			return i;
	return -1;
}

int proto_parse_panel(struct model *m, char *buf, int len)
{
	char *pos = buf, *end = buf + len, *f[MAX_FIELDS];
	struct entity *e;
	struct page *pg = NULL;
	int n;

	memset(m, 0, sizeof *m);
	n = next_line(&pos, end, f);
	if (header(m, f, n, 1))
		return PROTO_ERROR;
	while ((n = next_line(&pos, end, f)) > 0) {
		if (!strcmp(f[0], "P") && n >= 2) {
			if (m->npages == MAX_PAGES)
				break;
			pg = &m->pages[m->npages++];
			copy(pg->title, f[1], TEXT_LEN);
			pg->first = m->nent;
		} else if (!strcmp(f[0], "E") && n >= 7) {
			if (m->nent == MAX_ENT || !proto_valid_id(f[1]))
				continue;
			if (!pg || pg->count == PER_PAGE) {
				if (m->npages == MAX_PAGES)
					break;
				/* A page with more than PER_PAGE tiles continues under the same title. */
				pg = &m->pages[m->npages++];
				copy(pg->title, m->npages > 1 ? m->pages[m->npages - 2].title : m->title, TEXT_LEN);
				pg->first = m->nent;
			}
			e = &m->ent[m->nent++];
			copy(e->id, f[1], ID_LEN);
			e->icon = parse_icon(f[2]);
			e->action = strchr("tpc", f[3][0]) && f[3][0] && !f[3][1] ? f[3][0] : '-';
			e->on = parse_on(f[4]);
			copy(e->value, f[5], TEXT_LEN);
			copy(e->name, f[6], TEXT_LEN);
			e->dirty = 1;
			pg->count++;
		}
	}
	return PROTO_OK;
}

int proto_apply(struct model *m, char *buf, int len)
{
	char *pos = buf, *end = buf + len, *f[MAX_FIELDS];
	struct entity *e;
	int n, i;

	n = next_line(&pos, end, f);
	if (header(m, f, n, 0))
		return PROTO_ERROR;
	while ((n = next_line(&pos, end, f)) > 0) {
		if (!strcmp(f[0], "R"))
			return PROTO_RELOAD;
		if (strcmp(f[0], "S") || n < 4 || (i = proto_find(m, f[1])) < 0)
			continue;
		e = &m->ent[i];
		e->on = parse_on(f[2]);
		copy(e->value, f[3], TEXT_LEN);
		e->dirty = 1;
	}
	return PROTO_OK;
}
