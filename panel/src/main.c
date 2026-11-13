/* homtom panel: Home Assistant tiles on the framebuffer, talking to the host gateway over USB Ethernet.
 * Blocks in select() on the touch device and the gateway sockets; redraws only dirty tiles. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <linux/fb.h>
#include "http.h"
#include "ui.h"

#define EXIT_OPENTOM 10
#define BUF_SIZE 16384
#define MAX_QUEUE 8
#define POLL_WAIT 25

enum { C_IDLE, C_CONNECTING, C_SENDING, C_READING };
enum { K_PANEL, K_CHANGES, K_ACTION };

struct conn {
	int fd, state, kind;
	long deadline;
	char req[256];
	int req_len, sent;
	char buf[BUF_SIZE];
	int len;
};

struct action {
	char id[ID_LEN];
	char verb;		/* 't' toggle, '+' / '-' climate step */
};

static struct {
	const char *host, *panel_id, *fb_path, *ts_path, *cal_path;
	int port;
} cfg = { "192.168.1.100", "default", "/dev/fb", "/dev/input/event0",
	  "/mnt/sdcard/opentom/etc/pointercal", 8130 };

static struct model model;
static struct view view;
static struct canvas back;
static unsigned short *fb_mem;
static int fb_stride, fb_fd = -1, ts_fd = -1;
static struct conn poll_conn, act_conn;
static struct action queue[MAX_QUEUE];
static int queue_len, backoff = 1, bar_dirty, tile_dirty[PER_PAGE], full_dirty = 1;
static long retry_at, note_until, hold_until;
static struct touch touch;
static int press_x, press_y, press_hit = HIT_NONE;

/* Milliseconds since start, never 0: seconds since 1970 times 1000 overflow a 32-bit long. */
static long now_ms(void)
{
	static long start;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	if (!start)
		start = tv.tv_sec - 1;
	return (tv.tv_sec - start) * 1000L + tv.tv_usec / 1000;
}

static void fb_open(void)
{
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	long size, offset = 0;

	fb_fd = open(cfg.fb_path, O_RDWR);
	if (fb_fd < 0) {
		perror(cfg.fb_path);
		exit(1);
	}
	fb_stride = SCREEN_W;
	size = SCREEN_W * SCREEN_H * 2;
	/* The emulator's framebuffer is a plain file: the ioctls fail and the defaults hold. */
	if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &fix) == 0 && ioctl(fb_fd, FBIOGET_VSCREENINFO, &var) == 0) {
		if (var.xres != SCREEN_W || var.yres < SCREEN_H || var.bits_per_pixel != 16)
			fprintf(stderr, "panel: unexpected mode %ux%u/%u\n", var.xres, var.yres, var.bits_per_pixel);
		fb_stride = fix.line_length / 2;
		offset = (long)var.yoffset * fix.line_length;
		size = fix.smem_len;
	}
	fb_mem = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
	if (fb_mem == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	fb_mem += offset / 2;
}

static void flush(int x, int y, int w, int h)
{
	int j;
	for (j = y; j < y + h; j++)
		memcpy(fb_mem + j * fb_stride + x, back.px + j * SCREEN_W + x, w * 2);
}

static void load_calibration(void)
{
	char text[256];
	int fd = open(cfg.cal_path, O_RDONLY), n = -1;
	touch.cal.a[0] = touch.cal.a[4] = touch.cal.a[6] = 65536;
	if (fd >= 0) {
		n = read(fd, text, sizeof text - 1);
		close(fd);
	}
	if (n > 0) {
		text[n] = 0;
		if (calib_parse(&touch.cal, text) == 0)
			return;
	}
	fprintf(stderr, "panel: no usable %s, touch is uncalibrated\n", cfg.cal_path);
	touch.cal.a[1] = touch.cal.a[2] = touch.cal.a[3] = touch.cal.a[5] = 0;
	touch.cal.a[0] = touch.cal.a[4] = touch.cal.a[6] = 65536;
}

static void note(const char *text, int ms)
{
	strncpy(view.note, text, sizeof view.note - 1);
	note_until = ms ? now_ms() + ms : 0;
	bar_dirty = 1;
}

static void set_link(int link)
{
	if (view.link != link) {
		view.link = link;
		bar_dirty = 1;
	}
}

static void conn_close(struct conn *c)
{
	if (c->fd >= 0)
		close(c->fd);
	c->fd = -1;
	c->state = C_IDLE;
}

static int conn_start(struct conn *c, int kind, const char *method, const char *path, int timeout_s)
{
	struct sockaddr_in addr;
	char host[48];

	conn_close(c);
	sprintf(host, "%.32s:%d", cfg.host, cfg.port);
	c->req_len = http_request(c->req, sizeof c->req, method, host, path);
	if (c->req_len < 0)
		return -1;
	c->fd = socket(AF_INET, SOCK_STREAM, 0);
	if (c->fd < 0)
		return -1;
	fcntl(c->fd, F_SETFL, O_NONBLOCK);
	memset(&addr, 0, sizeof addr);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(cfg.port);
	addr.sin_addr.s_addr = inet_addr(cfg.host);
	c->kind = kind;
	c->sent = c->len = 0;
	c->deadline = now_ms() + timeout_s * 1000L;
	if (connect(c->fd, (struct sockaddr *)&addr, sizeof addr) < 0 && errno != EINPROGRESS) {
		conn_close(c);
		return -1;
	}
	c->state = C_CONNECTING;
	return 0;
}

static void request_panel(void)
{
	char path[128];
	sprintf(path, "/v1/panel?panel=%.40s", cfg.panel_id);
	if (conn_start(&poll_conn, K_PANEL, "GET", path, 6) < 0)
		retry_at = now_ms() + 1000;
}

static void request_changes(void)
{
	char path[160];
	sprintf(path, "/v1/changes?panel=%.40s&since=%lu&wait=%d", cfg.panel_id, model.seq, POLL_WAIT);
	if (conn_start(&poll_conn, K_CHANGES, "GET", path, POLL_WAIT + 10) < 0)
		retry_at = now_ms() + 1000;
}

static void finish_action(int ok, char *body, int len);

static void send_next_action(void)
{
	char path[160];
	struct action *a = &queue[0];
	if (!queue_len || act_conn.state != C_IDLE)
		return;
	if (a->verb == 't')
		sprintf(path, "/v1/toggle/%s?panel=%.40s", a->id, cfg.panel_id);
	else
		sprintf(path, "/v1/adjust/%s/%s?panel=%.40s", a->id, a->verb == '+' ? "up" : "down", cfg.panel_id);
	if (conn_start(&act_conn, K_ACTION, "POST", path, 6) < 0)
		finish_action(0, NULL, 0);
}

static void mark_entity(struct entity *e)
{
	int i, page = view.page;
	struct page *pg = &model.pages[page];
	for (i = 0; i < pg->count; i++)
		if (&model.ent[pg->first + i] == e)
			tile_dirty[i] = 1;
}

static void collect_dirty(void)
{
	int i;
	struct page *pg;
	if (!view.loaded)
		return;
	pg = &model.pages[view.page];
	for (i = 0; i < pg->count; i++)
		if (model.ent[pg->first + i].dirty)
			tile_dirty[i] = 1;
	for (i = 0; i < model.nent; i++)
		model.ent[i].dirty = 0;
}

static void link_failed(const char *why)
{
	if (view.link != LINK_OFFLINE)
		fprintf(stderr, "panel: gateway %s:%d %s\n", cfg.host, cfg.port, why);
	set_link(LINK_OFFLINE);
	retry_at = now_ms() + backoff * 1000L;
	if (backoff < 8)
		backoff *= 2;
	if (!view.loaded)
		full_dirty = 1;
}

static void finish_action(int ok, char *body, int len)
{
	struct action done = queue[0];
	int i = proto_find(&model, done.id);
	memmove(queue, queue + 1, (queue_len - 1) * sizeof queue[0]);
	queue_len--;
	if (i >= 0 && model.ent[i].pending > 0) {
		model.ent[i].pending--;
		mark_entity(&model.ent[i]);
	}
	if (!ok || proto_apply(&model, body, len) == PROTO_ERROR) {
		/* The optimistic state may be wrong now; fetch the truth. */
		note("action failed", 3000);
		if (poll_conn.state == C_IDLE || poll_conn.kind == K_CHANGES)
			request_panel();
	}
	send_next_action();
}

static void conn_done(struct conn *c, int ok)
{
	int status = 0, len = 0, rc;
	char *body = NULL;

	if (ok && (http_parse(c->buf, c->len, &status, &body, &len) < 0 || status != 200))
		ok = 0;
	if (ok)
		body[len] = 0;
	conn_close(c);
	if (c->kind == K_ACTION) {
		finish_action(ok, body, len);
		return;
	}
	if (!ok) {
		link_failed(status ? "returned an error" : "unreachable");
		return;
	}
	backoff = 1;
	set_link(LINK_LIVE);
	if (c->kind == K_PANEL) {
		if (proto_parse_panel(&model, body, len) != PROTO_OK) {
			link_failed("sent a bad panel");
			return;
		}
		view.loaded = 1;
		if (view.page >= model.npages)
			view.page = 0;
		full_dirty = 1;
		request_changes();
		return;
	}
	rc = proto_apply(&model, body, len);
	if (rc == PROTO_RELOAD)
		request_panel();
	else if (rc == PROTO_ERROR)
		link_failed("sent bad changes");
	else
		request_changes();
}

static void conn_io(struct conn *c, fd_set *rd, fd_set *wr)
{
	int n, err = 0;
	socklen_t elen = sizeof err;

	if (c->state == C_IDLE)
		return;
	if (c->state == C_CONNECTING && FD_ISSET(c->fd, wr)) {
		if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &err, &elen) < 0 || err) {
			conn_done(c, 0);
			return;
		}
		c->state = C_SENDING;
	}
	if (c->state == C_SENDING && FD_ISSET(c->fd, wr)) {
		n = send(c->fd, c->req + c->sent, c->req_len - c->sent, 0);
		if (n < 0 && errno != EAGAIN) {
			conn_done(c, 0);
			return;
		}
		if (n > 0 && (c->sent += n) == c->req_len)
			c->state = C_READING;
		return;
	}
	if (c->state == C_READING && FD_ISSET(c->fd, rd)) {
		n = read(c->fd, c->buf + c->len, BUF_SIZE - 1 - c->len);
		if (n == 0)
			conn_done(c, 1);
		else if (n < 0 && errno != EAGAIN)
			conn_done(c, 0);
		else if (n > 0 && (c->len += n) >= BUF_SIZE - 1)
			conn_done(c, 0);
	}
}

static void conn_fds(struct conn *c, fd_set *rd, fd_set *wr, int *maxfd)
{
	if (c->state == C_IDLE)
		return;
	FD_SET(c->fd, c->state == C_READING ? rd : wr);
	if (c->fd > *maxfd)
		*maxfd = c->fd;
}

static void enqueue(struct entity *e, char verb)
{
	if (queue_len == MAX_QUEUE) {
		note("busy", 1500);
		return;
	}
	strcpy(queue[queue_len].id, e->id);
	queue[queue_len++].verb = verb;
	e->pending++;
	send_next_action();
}

static void tap_tile(int slot, int x)
{
	struct entity *e = ui_slot_entity(&view, slot);
	struct rect r;
	int zone;

	if (!e || e->action == '-' || e->on == 'u')
		return;
	layout_tile(slot, &r);
	zone = e->action == 'c' ? layout_zone(&r, x) : 0;
	if (zone)
		enqueue(e, zone > 0 ? '+' : '-');
	else {
		if (e->action != 'p') {
			e->on = e->on == '1' ? '0' : '1';
			if (e->action == 't')
				strcpy(e->value, e->on == '1' ? "On" : "Off");
		}
		enqueue(e, 't');
	}
	tile_dirty[slot] = 1;
}

static void change_page(int delta)
{
	if (!view.loaded || model.npages < 2)
		return;
	view.page = (view.page + delta + model.npages) % model.npages;
	full_dirty = 1;
}

static void on_touch(int ev)
{
	int hit, g;
	if (ev == T_PRESS) {
		press_x = touch.x;
		press_y = touch.y;
		press_hit = layout_hit(touch.x, touch.y);
		hold_until = press_hit == HIT_CORNER ? now_ms() + LONG_PRESS_MS : 0;
		if (press_hit >= 0 && ui_slot_entity(&view, press_hit)) {
			view.pressed = press_hit;
			tile_dirty[press_hit] = 1;
		}
		return;
	}
	if (ev != T_RELEASE)
		return;
	hold_until = 0;
	if (view.pressed >= 0) {
		tile_dirty[view.pressed] = 1;
		view.pressed = -1;
	}
	g = layout_gesture(press_x, press_y, touch.x, touch.y);
	hit = layout_hit(touch.x, touch.y);
	if (g == G_LEFT)
		change_page(1);
	else if (g == G_RIGHT)
		change_page(-1);
	else if (g == G_TAP && hit == press_hit) {
		if (hit >= 0)
			tap_tile(hit, touch.x);
		else if (hit == HIT_PAGES)
			change_page(1);
	}
}

static void read_touch(void)
{
	/* struct input_event on 32-bit ARM: timeval, type, code, value. */
	struct { long sec, usec; unsigned short type, code; int value; } ev[16];
	int n, i, t;
	n = read(ts_fd, ev, sizeof ev);
	if (n <= 0)
		return;
	for (i = 0; i < n / (int)sizeof ev[0]; i++)
		if ((t = touch_feed(&touch, ev[i].type, ev[i].code, ev[i].value)) != T_NONE)
			on_touch(t);
}

static void redraw(void)
{
	struct rect r;
	char detail[64];
	int i;

	collect_dirty();
	if (full_dirty) {
		if (view.loaded) {
			ui_screen(&back, &view);
		} else {
			ui_bar(&back, &view);
			sprintf(detail, "gateway %.40s", view.gateway);
			ui_message(&back, view.link == LINK_OFFLINE ? "Waiting for the gateway" : "Connecting", detail);
		}
		flush(0, 0, SCREEN_W, SCREEN_H);
		full_dirty = bar_dirty = 0;
		memset(tile_dirty, 0, sizeof tile_dirty);
		return;
	}
	if (bar_dirty) {
		ui_bar(&back, &view);
		flush(0, 0, SCREEN_W, BAR_H);
		bar_dirty = 0;
	}
	for (i = 0; i < PER_PAGE; i++)
		if (tile_dirty[i]) {
			ui_tile(&back, &view, i);
			layout_tile(i, &r);
			flush(r.x, r.y, r.w, r.h);
			tile_dirty[i] = 0;
		}
}

static void usage(void)
{
	fprintf(stderr, "usage: homtom-panel [-g host[:port]] [-p panel] [-f fb] [-t touch] [-c pointercal]\n");
	exit(2);
}

static void parse_args(int argc, char **argv)
{
	static char host[40];
	char *colon;
	int i;
	for (i = 1; i < argc; i++) {
		if (i + 1 >= argc || argv[i][0] != '-' || argv[i][2])
			usage();
		switch (argv[i][1]) {
		case 'g':
			strncpy(host, argv[++i], sizeof host - 1);
			if ((colon = strchr(host, ':')) != NULL) {
				*colon = 0;
				cfg.port = atoi(colon + 1);
			}
			cfg.host = host;
			break;
		case 'p': cfg.panel_id = argv[++i]; break;
		case 'f': cfg.fb_path = argv[++i]; break;
		case 't': cfg.ts_path = argv[++i]; break;
		case 'c': cfg.cal_path = argv[++i]; break;
		default: usage();
		}
	}
	if (inet_addr(cfg.host) == INADDR_NONE || cfg.port <= 0)
		usage();
}

static long until(long t, long now, long best)
{
	if (t <= 0)
		return best;
	t -= now;
	if (t < 0)
		t = 0;
	return best < 0 || t < best ? t : best;
}

int main(int argc, char **argv)
{
	fd_set rd, wr;
	struct timeval tv;
	long now, wait;
	int maxfd;

	parse_args(argc, argv);
	signal(SIGPIPE, SIG_IGN);
	fb_open();
	back.w = SCREEN_W;
	back.h = SCREEN_H;
	back.px = malloc(SCREEN_W * SCREEN_H * 2);
	ts_fd = open(cfg.ts_path, O_RDONLY | O_NONBLOCK);
	if (!back.px || ts_fd < 0) {
		perror(cfg.ts_path);
		return 1;
	}
	load_calibration();
	poll_conn.fd = act_conn.fd = -1;
	view.m = &model;
	view.pressed = -1;
	sprintf(view.gateway, "%.32s:%d", cfg.host, cfg.port);
	request_panel();

	for (;;) {
		redraw();
		now = now_ms();
		FD_ZERO(&rd);
		FD_ZERO(&wr);
		FD_SET(ts_fd, &rd);
		maxfd = ts_fd;
		conn_fds(&poll_conn, &rd, &wr, &maxfd);
		conn_fds(&act_conn, &rd, &wr, &maxfd);
		wait = until(hold_until, now, -1);
		wait = until(note_until, now, wait);
		wait = until(retry_at, now, wait);
		if (poll_conn.state != C_IDLE)
			wait = until(poll_conn.deadline, now, wait);
		if (act_conn.state != C_IDLE)
			wait = until(act_conn.deadline, now, wait);
		tv.tv_sec = wait / 1000;
		tv.tv_usec = (wait % 1000) * 1000;
		if (select(maxfd + 1, &rd, &wr, NULL, wait < 0 ? NULL : &tv) < 0) {
			if (errno != EINTR)
				perror("select");
			continue;
		}
		if (FD_ISSET(ts_fd, &rd))
			read_touch();
		conn_io(&poll_conn, &rd, &wr);
		conn_io(&act_conn, &rd, &wr);

		now = now_ms();
		if (hold_until && now >= hold_until) {
			ui_message(&back, "Opening OpenTom", "restart homtom to come back");
			flush(0, 0, SCREEN_W, SCREEN_H);
			return EXIT_OPENTOM;
		}
		if (note_until && now >= note_until) {
			view.note[0] = 0;
			note_until = 0;
			bar_dirty = 1;
		}
		if (retry_at && now >= retry_at) {
			retry_at = 0;
			if (poll_conn.state == C_IDLE)
				request_panel();
		}
		if (poll_conn.state != C_IDLE && now >= poll_conn.deadline)
			conn_done(&poll_conn, 0);
		if (act_conn.state != C_IDLE && now >= act_conn.deadline)
			conn_done(&act_conn, 0);
	}
}
