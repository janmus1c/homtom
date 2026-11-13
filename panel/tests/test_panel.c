/* Host tests for the panel's parser, layout, touch and renderer; built with the Mac's clang.
 * test_panel                     run the checks
 * test_panel render FILE OUT     render the panel in FILE (gateway format) to raw RGB565 OUT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/http.h"
#include "../src/ui.h"

static int failures, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
	fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); } } while (0)

static const char PANEL[] =
	"H\t1\t1700000005\tHome\n"
	"P\tLiving room\n"
	"E\tlight.living_room\tlight\tt\t1\tOn \xc2\xb7 80%\tLiving room\n"
	"E\tswitch.coffee\tswitch\tt\t0\tOff\tCoffee machine\n"
	"E\tclimate.living\tclimate\tc\t1\tHeat \xc2\xb7 21.5\xc2\xb0\x43\tThermostat\n"
	"E\tsensor.outdoor\tsensor\t-\t0\t8.4\xc2\xb0\x43\tOutside\n"
	"E\tscene.movie\tscene\tp\t0\tScene\tMovie night\n"
	"E\tlight.kitchen\tlight\tt\tu\tUnavailable\tKitchen\n"
	"E\tlight.hall\tlight\tt\t0\tOff\tHallway\n"
	"P\tGarden\r\n"
	"E\tswitch.pump\tswitch\tt\t0\tOff\tPump\n"
	"E\tBad.Id\tswitch\tt\t0\tOff\tRejected\n"
	"X\tunknown record\n";

static int parse(struct model *m, const char *text)
{
	char buf[4096];
	strcpy(buf, text);
	return proto_parse_panel(m, buf, strlen(buf));
}

static int apply(struct model *m, const char *text)
{
	char buf[1024];
	strcpy(buf, text);
	return proto_apply(m, buf, strlen(buf));
}

static void test_proto(void)
{
	static struct model m;
	int i;

	CHECK(parse(&m, PANEL) == PROTO_OK);
	CHECK(m.seq == 1700000005UL);
	CHECK(!strcmp(m.title, "Home"));
	CHECK(m.nent == 8);
	/* Seven tiles on the first page overflow into a continuation page with the same title. */
	CHECK(m.npages == 3);
	CHECK(m.pages[0].count == 6 && m.pages[1].count == 1 && m.pages[2].count == 1);
	CHECK(!strcmp(m.pages[1].title, "Living room"));
	CHECK(!strcmp(m.pages[2].title, "Garden"));
	CHECK(m.ent[0].icon == IC_LIGHT && m.ent[0].action == 't' && m.ent[0].on == '1');
	CHECK(!strcmp(m.ent[0].value, "On \xc2\xb7 80%"));
	CHECK(m.ent[2].action == 'c' && m.ent[3].action == '-' && m.ent[4].action == 'p');
	CHECK(m.ent[5].on == 'u');
	CHECK(proto_find(&m, "Bad.Id") < 0);
	CHECK(proto_find(&m, "switch.pump") == 7);

	for (i = 0; i < m.nent; i++)
		m.ent[i].dirty = 0;
	CHECK(apply(&m, "H\t1\t1700000009\nS\tswitch.coffee\t1\tOn\nS\tnot.here\t1\tx\n") == PROTO_OK);
	CHECK(m.seq == 1700000009UL);
	CHECK(m.ent[1].on == '1' && !strcmp(m.ent[1].value, "On") && m.ent[1].dirty);
	CHECK(!m.ent[0].dirty);
	CHECK(apply(&m, "H\t1\t1700000010\nR\n") == PROTO_RELOAD);
	CHECK(apply(&m, "garbage") == PROTO_ERROR);
	CHECK(apply(&m, "H\t2\t5\n") == PROTO_ERROR);
	CHECK(parse(&m, "H\t1\tnope\n") == PROTO_ERROR);
	CHECK(parse(&m, "") == PROTO_ERROR);

	/* Long values are cut on a character boundary, never inside a UTF-8 sequence. */
	CHECK(parse(&m, "H\t1\t1\n"
		  "E\tsensor.x\tsensor\t-\t0\t123456789012345678901234567890\xc2\xb0\tLong\n") == PROTO_OK);
	CHECK(strlen(m.ent[0].value) == 30);
	CHECK(proto_valid_id("light.a_b2") && !proto_valid_id("light") && !proto_valid_id("a.b.c")
	      && !proto_valid_id("light.x y") && !proto_valid_id("light.X"));
}

static void test_http(void)
{
	char buf[256], *body;
	int status, len;

	strcpy(buf, "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 5\r\n\r\nhello world");
	CHECK(http_parse(buf, strlen(buf), &status, &body, &len) == 0);
	CHECK(status == 200 && len == 5 && !strncmp(body, "hello", 5));
	strcpy(buf, "HTTP/1.1 404 Not Found\n\nnope");
	CHECK(http_parse(buf, strlen(buf), &status, &body, &len) == 0);
	CHECK(status == 404 && len == 4);
	strcpy(buf, "HTTP/1.0 200 OK\r\nContent-Length: 50\r\n\r\nshort");
	CHECK(http_parse(buf, strlen(buf), &status, &body, &len) < 0);
	strcpy(buf, "HTTP/1.0 200 OK\r\nno end");
	CHECK(http_parse(buf, strlen(buf), &status, &body, &len) < 0);
	CHECK(http_parse("SSH-2.0", 7, &status, &body, &len) < 0);
	CHECK(http_request(buf, sizeof buf, "POST", "192.168.1.100:8130", "/v1/toggle/light.a") > 0);
	CHECK(strstr(buf, "POST /v1/toggle/light.a HTTP/1.0\r\n") == buf && strstr(buf, "Content-Length: 0\r\n\r\n"));
	CHECK(http_request(buf, 16, "GET", "h", "/very/long/path/that/does/not/fit") < 0);
}

static void test_layout(void)
{
	struct rect r, s;
	int i, j;

	for (i = 0; i < PER_PAGE; i++) {
		layout_tile(i, &r);
		CHECK(r.x >= GAP && r.y >= BAR_H && r.x + r.w <= SCREEN_W - GAP && r.y + r.h <= SCREEN_H - GAP + 1);
		CHECK(r.w >= 140 && r.h >= 100);
		CHECK(layout_hit(r.x + r.w / 2, r.y + r.h / 2) == i);
		for (j = 0; j < i; j++) {
			layout_tile(j, &s);
			CHECK(!rect_overlap(&r, &s));
		}
	}
	layout_tile(2, &r);
	CHECK(r.x + r.w == SCREEN_W - GAP);
	CHECK(layout_hit(GAP / 2, 100) == HIT_NONE);
	CHECK(layout_hit(10, 10) == HIT_CORNER);
	CHECK(layout_hit(240, 10) == HIT_PAGES);
	CHECK(layout_hit(470, 10) == HIT_BAR);
	CHECK(layout_zone(&r, r.x + 5) == -1 && layout_zone(&r, r.x + r.w / 2) == 0 && layout_zone(&r, r.x + r.w - 5) == 1);
	CHECK(layout_gesture(100, 100, 104, 103) == G_TAP);
	CHECK(layout_gesture(300, 100, 150, 120) == G_LEFT);
	CHECK(layout_gesture(100, 100, 250, 90) == G_RIGHT);
	CHECK(layout_gesture(100, 100, 120, 200) == G_NONE);
}

static void test_touch(void)
{
	struct touch t;
	struct calib c;
	int x, y;

	/* The GO 630's own pointercal from /mnt/sdcard/opentom/etc/pointercal. */
	CHECK(calib_parse(&c, "-34898 168 33229056 -253 -22440 19888250 65536 # comment") == 0);
	calib_apply(&c, 952, 886, &x, &y);
	CHECK(x <= 2 && y <= 2);
	calib_apply(&c, 52, 90, &x, &y);
	CHECK(x >= 470 && y >= 260);
	calib_apply(&c, 5000, -5000, &x, &y);
	CHECK(x == 0 && y == SCREEN_H - 1);
	CHECK(calib_parse(&c, "1 2 3") < 0);
	CHECK(calib_parse(&c, "1 0 0 0 1 0 0") < 0);

	memset(&t, 0, sizeof t);
	t.cal.a[0] = t.cal.a[4] = t.cal.a[6] = 1;
	/* Device driver: pressure 1023 while down, 0 on release, no BTN_TOUCH. */
	CHECK(touch_feed(&t, 3, 0, 120) == T_NONE);
	touch_feed(&t, 3, 1, 80);
	touch_feed(&t, 3, 0x18, 1023);
	CHECK(touch_feed(&t, 0, 0, 0) == T_PRESS && t.x == 120 && t.y == 80);
	touch_feed(&t, 3, 0, 125);
	CHECK(touch_feed(&t, 0, 0, 0) == T_MOVE && t.x == 125);
	touch_feed(&t, 3, 0x18, 0);
	CHECK(touch_feed(&t, 0, 0, 0) == T_RELEASE && t.x == 125);
	CHECK(touch_feed(&t, 0, 0, 0) == T_NONE);
	/* Emulator bridge: BTN_TOUCH plus pressure. */
	touch_feed(&t, 3, 0, 10);
	touch_feed(&t, 3, 0x18, 255);
	touch_feed(&t, 1, 0x14a, 1);
	CHECK(touch_feed(&t, 0, 0, 0) == T_PRESS);
	touch_feed(&t, 1, 0x14a, 0);
	touch_feed(&t, 3, 0x18, 0);
	CHECK(touch_feed(&t, 0, 0, 0) == T_RELEASE);
}

static void test_render(void)
{
	static unsigned short px[SCREEN_W * SCREEN_H];
	static struct model m;
	struct canvas c;
	struct view v;
	struct rect r;
	unsigned short ink = RGB(0x0E, 0x16, 0x1E), blue = RGB(0x18, 0xBC, 0xF2);
	int i, blue_px = 0;

	c.px = px;
	c.w = SCREEN_W;
	c.h = SCREEN_H;
	memset(&v, 0, sizeof v);
	parse(&m, PANEL);
	v.m = &m;
	v.loaded = 1;
	v.pressed = -1;
	v.link = LINK_LIVE;
	ui_screen(&c, &v);
	CHECK(px[SCREEN_H / 2 * SCREEN_W + 2] == ink);
	CHECK(gfx_text_width(&font_label, "homtom", 0) > 40);
	CHECK(gfx_text_width(&font_label, "", 0) == 0);
	/* The "on" light tile carries HA blue in its icon disc; the off switch tile none. */
	layout_tile(0, &r);
	for (i = 0; i < r.w * r.h; i++)
		blue_px += px[(r.y + i / r.w) * SCREEN_W + r.x + i % r.w] == blue;
	CHECK(blue_px > 400);
	layout_tile(1, &r);
	blue_px = 0;
	for (i = 0; i < r.w * r.h; i++)
		blue_px += px[(r.y + i / r.w) * SCREEN_W + r.x + i % r.w] == blue;
	CHECK(blue_px == 0);
	/* Text is clipped to its width with an ellipsis. */
	CHECK(gfx_text(&c, &font_label, 0, 100, "A very long entity name indeed", ink, 0, 80) <= 80);
}

static int render(const char *in, const char *out)
{
	static unsigned short px[SCREEN_W * SCREEN_H];
	static char buf[16384];
	static struct model m;
	struct canvas c;
	struct view v;
	FILE *f = fopen(in, "rb");
	int n, page = getenv("PAGE") ? atoi(getenv("PAGE")) : 0;

	if (!f)
		return 1;
	n = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	memset(&v, 0, sizeof v);
	v.m = &m;
	v.pressed = -1;
	v.link = LINK_LIVE;
	if (proto_parse_panel(&m, buf, n) != PROTO_OK)
		return 1;
	v.loaded = 1;
	v.page = page < m.npages ? page : 0;
	c.px = px;
	c.w = SCREEN_W;
	c.h = SCREEN_H;
	ui_screen(&c, &v);
	f = fopen(out, "wb");
	if (!f)
		return 1;
	fwrite(px, 2, SCREEN_W * SCREEN_H, f);
	fclose(f);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 4 && !strcmp(argv[1], "render"))
		return render(argv[2], argv[3]);
	test_proto();
	test_http();
	test_layout();
	test_touch();
	test_render();
	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
