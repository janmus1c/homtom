/* Host test of homtom-keyd-logic.h with fake event streams: cc -O1 -Wall -o /tmp/keyd_test tests/keyd_test.c && /tmp/keyd_test */
#include <stdio.h>
#include "../homtom-keyd-logic.h"

static int bad, cases;

struct step {
	unsigned int t;	/* offset from the scenario start, ms */
	int ev;		/* -1 tick only; 0, 1, 2 = key value */
};

static const char *nm(enum kd_action a)
{
	static const char *n[] = { "none", "short", "long", "hold5", "reboot", "poweroff" };
	return n[a];
}

/* Runs the steps like the daemon: tick first, then the event. Collects the actions in order. */
static int run(const char *name, unsigned int base, int down_at_start, const struct step *s, int n,
	       const enum kd_action *want, int nwant)
{
	struct kd k;
	enum kd_action got[16];
	int ng = 0, i, ok = 1;

	enum kd_action w[16];
	int nw = 0;

	for (i = 0; i < nwant; i++)	/* W(KD_NONE) stands for "no action at all" */
		if (want[i] != KD_NONE)
			w[nw++] = want[i];
	want = w;
	nwant = nw;
	kd_start(&k, base, down_at_start);
	for (i = 0; i < n; i++) {
		unsigned int now = base + s[i].t;
		enum kd_action a = kd_tick(&k, now);

		if (a != KD_NONE && ng < 16)
			got[ng++] = a;
		if (s[i].ev >= 0) {
			a = kd_event(&k, s[i].ev, now);
			if (a != KD_NONE && ng < 16)
				got[ng++] = a;
		}
	}
	if (ng != nwant)
		ok = 0;
	for (i = 0; ok && i < ng; i++)
		if (got[i] != want[i])
			ok = 0;
	cases++;
	if (!ok) {
		bad++;
		printf("FAIL %s (base %u): got", name, base);
		for (i = 0; i < ng; i++)
			printf(" %s", nm(got[i]));
		printf(", want");
		for (i = 0; i < nwant; i++)
			printf(" %s", nm(want[i]));
		printf("\n");
	}
	return ok;
}

#define RUN(name, start, steps, want) do { \
	const struct step s_[] = steps; const enum kd_action w_[] = want; \
	static const unsigned int bases_[] = { 1000, 0, 0xFFFFFF00u, 0xFFFFFFFFu - 4999u, 0xFFFFFFFFu - 9999u, 0xFFFFD000u }; \
	unsigned int b_; \
	for (b_ = 0; b_ < sizeof(bases_) / sizeof(bases_[0]); b_++) \
		run(name, bases_[b_], start, s_, sizeof(s_) / sizeof(s_[0]), w_, sizeof(w_) / sizeof(w_[0])); \
} while (0)

#define W(...) { __VA_ARGS__ }
#define S(...) { __VA_ARGS__ }

int main(void)
{
	/* every scenario runs at six clock bases, three of them straddling the 2^32 wrap */
	RUN("short 100 ms", 0, S({ 0, 1 }, { 100, 0 }), W(KD_SHORT));
	RUN("short 799 ms", 0, S({ 0, 1 }, { 799, 0 }), W(KD_SHORT));
	RUN("800 ms is no action", 0, S({ 0, 1 }, { 800, 0 }), W(KD_NONE));
	RUN("1499 ms is no action", 0, S({ 0, 1 }, { 1499, 0 }), W(KD_NONE));
	RUN("long 1500 ms", 0, S({ 0, 1 }, { 1500, 0 }), W(KD_LONG));
	RUN("long 4999 ms", 0, S({ 0, 1 }, { 4999, 0 }), W(KD_LONG));
	RUN("contact noise 20 ms", 0, S({ 0, 1 }, { 20, 0 }), W(KD_NONE));
	RUN("hold5 hint at 5 s, once", 0, S({ 0, 1 }, { 5000, -1 }, { 5001, -1 }, { 7000, -1 }, { 9000, -1 }), W(KD_HOLD5));
	RUN("release at 5000 ms powers off", 0, S({ 0, 1 }, { 5000, -1 }, { 5000, 0 }), W(KD_HOLD5, KD_POWEROFF));
	RUN("release at 7 s powers off", 0, S({ 0, 1 }, { 5000, -1 }, { 7000, 0 }), W(KD_HOLD5, KD_POWEROFF));
	RUN("release at 9999 ms powers off", 0, S({ 0, 1 }, { 5000, -1 }, { 9999, 0 }), W(KD_HOLD5, KD_POWEROFF));
	RUN("hold to 10 s reboots once", 0, S({ 0, 1 }, { 5000, -1 }, { 10000, -1 }, { 10010, -1 }, { 12000, -1 }), W(KD_HOLD5, KD_REBOOT));
	RUN("release after the 10 s reboot does nothing", 0, S({ 0, 1 }, { 5000, -1 }, { 10000, -1 }, { 10400, 0 }), W(KD_HOLD5, KD_REBOOT));
	RUN("release at 15 s after reboot: no power off", 0, S({ 0, 1 }, { 5000, -1 }, { 10000, -1 }, { 15000, 0 }), W(KD_HOLD5, KD_REBOOT));
	RUN("release at 10000 ms (tick first) is the reboot", 0, S({ 0, 1 }, { 5000, -1 }, { 10000, 0 }), W(KD_HOLD5, KD_REBOOT));
	RUN("release just under 5 s is long, not power off", 0, S({ 0, 1 }, { 4999, -1 }, { 4999, 0 }), W(KD_LONG));
	RUN("repeats ignored in a short press", 0, S({ 0, 1 }, { 300, 2 }, { 350, 2 }, { 400, 0 }), W(KD_SHORT));
	RUN("repeats do not stretch a hold", 0, S({ 0, 1 }, { 500, 2 }, { 1000, 2 }, { 1500, 2 }, { 2000, 0 }), W(KD_LONG));
	RUN("repeat while up ignored", 0, S({ 0, 2 }, { 100, 2 }), W(KD_NONE));
	RUN("release without press ignored", 0, S({ 0, 0 }, { 100, 0 }), W(KD_NONE));
	RUN("duplicate press ignored", 0, S({ 0, 1 }, { 100, 1 }, { 200, 1 }, { 300, 0 }), W(KD_SHORT));
	RUN("duplicate release ignored", 0, S({ 0, 1 }, { 100, 0 }, { 200, 0 }), W(KD_SHORT));
	RUN("two short presses", 0, S({ 0, 1 }, { 100, 0 }, { 500, 1 }, { 600, 0 }), W(KD_SHORT, KD_SHORT));
	RUN("press within 30 ms of a release is bounce", 0, S({ 0, 1 }, { 100, 0 }, { 110, 1 }, { 200, 0 }), W(KD_SHORT));
	RUN("bounced press cannot grow into a hold", 0, S({ 0, 1 }, { 100, 0 }, { 110, 1 }, { 5200, -1 }, { 10200, -1 }, { 11000, 0 }), W(KD_SHORT));
	RUN("press after 30 ms counts", 0, S({ 0, 1 }, { 100, 0 }, { 130, 1 }, { 200, 0 }), W(KD_SHORT, KD_SHORT));
	RUN("short then long", 0, S({ 0, 1 }, { 200, 0 }, { 1000, 1 }, { 3000, 0 }), W(KD_SHORT, KD_LONG));
	RUN("long then power-off hold", 0, S({ 0, 1 }, { 2000, 0 }, { 3000, 1 }, { 8000, -1 }, { 8100, 0 }), W(KD_LONG, KD_HOLD5, KD_POWEROFF));
	RUN("key down at start: release does nothing", 1, S({ 100, 0 }), W(KD_NONE));
	RUN("key down at start: no hint, no reboot, no power off", 1, S({ 5000, -1 }, { 10000, -1 }, { 12000, 0 }), W(KD_NONE));
	RUN("key down at start: next press works", 1, S({ 100, 0 }, { 500, 1 }, { 600, 0 }), W(KD_SHORT));
	RUN("stuck key 30 s: nothing more after the reboot", 0, S({ 0, 1 }, { 5000, -1 }, { 10000, -1 }, { 30000, -1 }, { 40000, -1 }, { 50000, 0 }), W(KD_HOLD5, KD_REBOOT));
	RUN("stalled past 10 s: late reboot dropped, release never powers off", 0, S({ 0, 1 }, { 5000, -1 }, { 29000, -1 }, { 31000, -1 }, { 32000, 0 }), W(KD_HOLD5));
	RUN("stalled daemon: late hint dropped, release does nothing", 0, S({ 0, 1 }, { 8000, -1 }, { 9000, 0 }), W(KD_NONE));
	RUN("stalled daemon: late reboot dropped", 0, S({ 0, 1 }, { 5000, -1 }, { 13000, -1 }, { 13100, 0 }), W(KD_HOLD5));
	RUN("long gap before the first tick", 0, S({ 0, 1 }, { 60000, -1 }, { 60100, 0 }), W(KD_NONE));
	RUN("press after a power-off release starts clean", 0, S({ 0, 1 }, { 5000, -1 }, { 6000, 0 }, { 7000, 1 }, { 7100, 0 }), W(KD_HOLD5, KD_POWEROFF, KD_SHORT));
	RUN("press after the reboot hold starts clean", 0, S({ 0, 1 }, { 5000, -1 }, { 10000, -1 }, { 11000, 0 }, { 12000, 1 }, { 12100, 0 }), W(KD_HOLD5, KD_REBOOT, KD_SHORT));
	RUN("release at 4999 then press again", 0, S({ 0, 1 }, { 4999, 0 }, { 5100, 1 }, { 5200, 0 }), W(KD_LONG, KD_SHORT));
	RUN("a held key after a long one: fresh timers", 0, S({ 0, 1 }, { 3000, 0 }, { 3100, 1 }, { 8100, -1 }), W(KD_LONG, KD_HOLD5));

	/* kd_timeout: infinite when idle, remaining time while held, 0 when due */
	{
		struct kd k;
		unsigned int base;
		static const unsigned int bases[] = { 100, 0xFFFFFF00u };
		int i;

		for (i = 0; i < 2; i++) {
			base = bases[i];
			kd_start(&k, base, 0);
			cases++;
			if (kd_timeout(&k, base) != -1) { bad++; printf("FAIL timeout idle\n"); }
			kd_event(&k, 1, base + 10);
			cases++;
			if (kd_timeout(&k, base + 110) != 4900) { bad++; printf("FAIL timeout held: %d\n", kd_timeout(&k, base + 110)); }
			cases++;
			if (kd_timeout(&k, base + 5010) != 0) { bad++; printf("FAIL timeout due\n"); }
			kd_tick(&k, base + 5010);
			cases++;
			if (kd_timeout(&k, base + 5010) != 5000) { bad++; printf("FAIL timeout after hold5: %d\n", kd_timeout(&k, base + 5010)); }
			kd_tick(&k, base + 10010);
			cases++;
			if (kd_timeout(&k, base + 10010) != 20000) { bad++; printf("FAIL timeout stuck wait: %d\n", kd_timeout(&k, base + 10010)); }
			kd_event(&k, 0, base + 10500);
			cases++;
			if (kd_timeout(&k, base + 10500) != -1) { bad++; printf("FAIL timeout after release\n"); }
		}
	}

	printf("%d cases, %d failed\n", cases, bad);
	return bad != 0;
}
