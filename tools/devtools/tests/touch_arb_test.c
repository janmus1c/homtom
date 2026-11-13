/* Host test of the homtom-touch ADC pause (docs/ADC_ARBITRATION.md) against mocked syscalls, clocks and frames:
 *   cc -O1 -Wall -Wno-unused-function -o /tmp/ta tests/touch_arb_test.c && /tmp/ta
 * The mock plays homtom-statusd: it posts /tmp/adc.req and removes it (and the acknowledgement) a set time after the
 * acknowledgement appears, or never. */
#include <stdio.h>
#include <string.h>

#define TOUCH_HOST_TEST
#define TOUCH_FRAME test_frame
static int test_frame(int *x, int *y, int *z);
#include "../homtom-touch.c"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* ---- mock world ---- */
static unsigned long long mono_ns;
#define RT_BASE 1762500000u
static int req, ack, release_ms = -1;
static u32 req_s, req_ns, req_ino = 100;
static unsigned long long ack_at;
static int acks, req_unlinks, stats, calls;
static char holder[32];
static int holder_len, holder_wfd;
static long holder_roff;

static u32 mono_ms(void)
{
	return (u32)(mono_ns / 1000000u);
}

static void advance(unsigned long long ns)
{
	mono_ns += ns;
	if (ack && release_ms >= 0 && mono_ns - ack_at >= (unsigned long long)release_ms * 1000000u)
		ack = req = 0;		/* statusd: sampled, removes adc.ack then adc.req */
}

/* statusd posts a request whose mtime is `age_ms` before now (negative: in the future) */
static void post(int age_ms, int rel)
{
	long long t = (long long)RT_BASE * 1000 + (long long)mono_ms() - age_ms;

	req = 1;
	req_ino++;
	req_s = (u32)(t / 1000);
	req_ns = (u32)(t % 1000) * 1000000u;
	release_ms = rel;
}

static long sc(long n, long a, long b, long c, long d, long e, long f)
{
	const char *p = (const char *)a;

	(void)d; (void)e; (void)f;
	calls++;
	switch (n) {
	case SYS_STAT64:
		stats++;
		if (!strcmp(p, ADC_REQ) && req) {
			u32 *sb = (u32 *)b;

			memset(sb, 0, 104);
			sb[20] = req_s;
			sb[21] = req_ns;
			sb[24] = req_ino;
			return 0;
		}
		return -2;
	case SYS_ACCESS:
		return (!strcmp(p, ADC_REQ) && req) || (!strcmp(p, ADC_ACK) && ack) ? 0 : -2;
	case SYS_OPEN:
		if (!strcmp(p, ADC_ACK) && (b & O_CREAT)) {
			if (!ack) {
				ack = 1;
				ack_at = mono_ns;
				acks++;
			}
			return 7;
		}
		if (!strcmp(p, ADC_HOLDER)) {
			if (b & O_CREAT) {
				holder_len = 0;
				holder_wfd = 1;
				return 8;
			}
			holder_roff = 0;
			return holder_len ? 9 : -2;
		}
		return -2;
	case SYS_WRITE:
		if (a == 8 && holder_wfd && holder_len + c < (long)sizeof(holder)) {
			memcpy(holder + holder_len, (const void *)b, (size_t)c);
			holder_len += (int)c;
		}
		return c;
	case SYS_READ:
		if (a == 9) {
			long k = holder_len - holder_roff < c ? holder_len - holder_roff : c;

			memcpy((void *)b, holder + holder_roff, (size_t)k);
			holder_roff += k;
			return k;
		}
		return 0;
	case SYS_CLOSE:
		if (a == 8)
			holder_wfd = 0;
		return 0;
	case SYS_UNLINK:
		if (!strcmp(p, ADC_REQ) && req) {
			req = 0;
			req_unlinks++;
			return 0;
		}
		if (!strcmp(p, ADC_ACK) && ack) {
			ack = 0;
			return 0;
		}
		if (!strcmp(p, ADC_HOLDER) && holder_len) {
			holder_len = 0;
			return 0;
		}
		return -2;
	case SYS_NANOSLEEP: {
		const long *ts = (const long *)a;

		advance((unsigned long long)ts[0] * 1000000000u + (unsigned long long)ts[1]);
		return 0;
	}
	case SYS_CLOCK_GETTIME: {
		long *ts = (long *)b;
		unsigned long long t = mono_ns + (a == 0 ? (unsigned long long)RT_BASE * 1000000000u : 0);

		ts[0] = (long)(t / 1000000000u);
		ts[1] = (long)(t % 1000000000u);
		return 0;
	}
	case SYS_GETPID:
		return 4242;
	}
	return -38;
}

/* ---- scripted frames: pressed or not, raw position; each frame takes 12 ms ---- */
static int f_pressed, f_x, f_y, frames;

static int test_frame(int *x, int *y, int *z)
{
	frames++;
	advance(12000000u);
	if (!f_pressed)
		return 0;
	*x = f_x;
	*y = f_y;
	*z = 50;
	return 1;
}

static int poll_with(int pressed, int x, int y)
{
	f_pressed = pressed;
	f_x = x;
	f_y = y;
	return tpoll();
}

static void reset_touch(void)
{
	tdown = dn_cnt = up_cnt = 0;
	req = ack = 0;
	release_ms = -1;
}

int main(void)
{
	int i, e, fr, c0, s0, a0;
	u32 t0;

	mono_ns = 5000000000ull;

	/* no request: one stat64 and nothing else per frame */
	reset_touch();
	c0 = calls;
	s0 = stats;
	for (i = 0; i < 10; i++)
		CHECK(poll_with(0, 0, 0) == EV_NONE);
	CHECK(frames == 10 && stats - s0 == 10 && calls - c0 == 10 && acks == 0);

	/* DOWN after three agreeing frames, then a pause: held down, no frame, no UP, acknowledgement created and gone */
	CHECK(poll_with(1, 100, 200) == EV_NONE);
	CHECK(poll_with(1, 101, 201) == EV_NONE);
	CHECK(poll_with(1, 102, 200) == EV_DOWN && tdown);
	post(0, 7);
	fr = frames;
	t0 = mono_ms();
	CHECK(poll_with(0, 0, 0) == EV_NONE);		/* released during the pause would be visible only in a frame */
	CHECK(frames == fr && tdown == 1 && up_cnt == 0 && acks == 1 && !ack && !req);
	CHECK(mono_ms() - t0 >= 7 && mono_ms() - t0 <= 12);
	CHECK(poll_with(1, 104, 202) == EV_MOVE && tdown);
	CHECK(arb_pauses == 1 && arb_overruns == 0);

	/* pauses never count towards the UP debounce, in either order */
	CHECK(poll_with(0, 0, 0) == EV_NONE && up_cnt == 1);
	post(0, 3);
	CHECK(poll_with(0, 0, 0) == EV_NONE && up_cnt == 1 && tdown);
	post(0, 3);
	CHECK(poll_with(0, 0, 0) == EV_NONE && up_cnt == 1 && tdown);
	CHECK(poll_with(1, 104, 202) == EV_MOVE && up_cnt == 0);	/* contact back: no spurious UP was sent */
	CHECK(poll_with(0, 0, 0) == EV_NONE);
	post(0, 3);
	CHECK(poll_with(0, 0, 0) == EV_NONE && tdown);
	CHECK(poll_with(0, 0, 0) == EV_UP && !tdown);		/* the second real released frame */
	CHECK(acks == 4);

	/* the DOWN settle count survives a pause, and a railed first sample still restarts it */
	reset_touch();
	CHECK(poll_with(1, 300, 400) == EV_NONE && dn_cnt == 1);
	CHECK(poll_with(1, 301, 400) == EV_NONE && dn_cnt == 2);
	post(0, 3);
	CHECK(poll_with(1, 301, 400) == EV_NONE && dn_cnt == 2 && !tdown);
	CHECK(poll_with(1, 302, 401) == EV_DOWN);
	reset_touch();
	CHECK(poll_with(1, 1023, 0) == EV_NONE && dn_cnt == 1);
	post(0, 3);
	CHECK(poll_with(1, 300, 400) == EV_NONE && dn_cnt == 1);	/* the pause */
	CHECK(poll_with(1, 300, 400) == EV_NONE && dn_cnt == 1);	/* far from the railed sample: settle restarts */
	CHECK(poll_with(1, 300, 400) == EV_NONE && dn_cnt == 2);
	CHECK(poll_with(1, 300, 400) == EV_DOWN);

	/* leftover requests (statusd died holding one): 3 s old or 3 s in the future are removed, no pause */
	reset_touch();
	a0 = acks;
	post(3000, -1);
	fr = frames;
	CHECK(poll_with(0, 0, 0) == EV_NONE && frames == fr + 1 && !req && req_unlinks == 1 && acks == a0);
	post(-3000, -1);
	CHECK(poll_with(0, 0, 0) == EV_NONE && frames == fr + 2 && !req && req_unlinks == 2 && acks == a0);
	post(1500, 3);					/* 1.5 s old is still a request */
	CHECK(poll_with(0, 0, 0) == EV_NONE && frames == fr + 2 && acks == a0 + 1);

	/* statusd never removes its request: resume after 400 ms, ignore that request, honour the next one */
	reset_touch();
	CHECK(poll_with(1, 50, 60) == EV_NONE);
	CHECK(poll_with(1, 50, 60) == EV_NONE);
	CHECK(poll_with(1, 50, 60) == EV_DOWN);
	post(0, -1);
	t0 = mono_ms();
	fr = frames;
	CHECK(poll_with(0, 0, 0) == EV_NONE && frames == fr && tdown);
	CHECK(mono_ms() - t0 >= ARB_PAUSE_MS && mono_ms() - t0 <= ARB_PAUSE_MS + ARB_STEP_MS);
	CHECK(arb_overruns == 1 && req && !ack);
	s0 = stats;
	CHECK(poll_with(1, 50, 60) == EV_MOVE && frames == fr + 1);	/* same request: ignored, sampling goes on */
	CHECK(poll_with(1, 51, 60) == EV_MOVE && stats - s0 == 2);
	for (i = 0; i < 200 && req; i++)
		e = poll_with(1, 51, 60);
	CHECK(!req && mono_ms() - t0 > ARB_STALE_MS && mono_ms() - t0 < ARB_STALE_MS + 1100);	/* removed once stale */
	post(0, 3);
	a0 = acks;
	fr = frames;
	CHECK(poll_with(0, 0, 0) == EV_NONE && frames == fr && acks == a0 + 1 && tdown);
	(void)e;

	/* holder file: our pid, removed on exit only while it is still ours */
	holder_write();
	CHECK(holder_len == 5 && !memcmp(holder, "4242\n", 5) && holder_pid() == 4242);
	holder_drop();
	CHECK(holder_len == 0);
	memcpy(holder, "777\n", 4);
	holder_len = 4;
	holder_drop();
	CHECK(holder_len == 4);

	if (fails) {
		printf("%d failure(s)\n", fails);
		return 1;
	}
	printf("touch_arb_test: all checks passed (%d pauses, %d timeouts)\n", (int)arb_pauses, (int)arb_overruns);
	return 0;
}
