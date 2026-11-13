/* Power-key decision logic for homtom-keyd. Pure: no syscalls, no libc, no division, so the device build and the host
 * test (tests/keyd_test.c) compile the same code. Times are u32 milliseconds from any monotonic clock; every difference is
 * taken modulo 2^32, so a clock wrap does not matter.
 *
 * Press timeline (one press = one down edge to the next up edge):
 *   released < 800 ms            KD_SHORT     on release
 *   released 800 .. < 1500 ms    nothing
 *   released 1500 ms .. < 5 s    KD_LONG      on release
 *   held to 5 s                  KD_HOLD5     once, while held (UI hint "release to power off")
 *   released 5 s .. < 10 s       KD_POWEROFF  on release (the key must be up before the reset: TTBL boots on when it
 *                                finds the key held, even with INFORM0 saying "Power down")
 *   held to 10 s                 KD_REBOOT    once, while held (emergency reboot to the storage window; works with a
 *                                frozen GUI); the release afterwards does nothing
 * Safety: auto-repeat (value 2) and duplicate edges are ignored; a press released within 30 ms is contact noise and does
 * nothing; a down edge within 30 ms of the previous release is ignored together with its release; a key found down at
 * start is ignored until it is released; a key held for 30 s is declared stuck and ignored until released; a hold mark
 * noticed more than 2 s after its deadline (stalled daemon, clock jump) is dropped and the press is ignored.
 */
#ifndef HOMTOM_KEYD_LOGIC_H
#define HOMTOM_KEYD_LOGIC_H

#define KD_SHORT_MAX_MS		800u
#define KD_LONG_MIN_MS		1500u
#define KD_HOLD5_MS		5000u
#define KD_REBOOT_MS		10000u
#define KD_STUCK_MS		30000u
#define KD_DEBOUNCE_MS		30u
#define KD_LATE_MS		2000u

enum kd_action { KD_NONE = 0, KD_SHORT, KD_LONG, KD_HOLD5, KD_REBOOT, KD_POWEROFF };

struct kd {
	unsigned char down;	/* a press is in progress */
	unsigned char ignore;	/* this press does nothing (stuck, late, start state) */
	unsigned char did5;
	unsigned char did10;
	unsigned char have_up;	/* t_up is valid */
	unsigned int t_down;
	unsigned int t_up;
};

static void kd_start(struct kd *k, unsigned int now, int key_down_at_start)
{
	k->down = (unsigned char)(key_down_at_start != 0);
	k->ignore = k->down;
	k->did5 = k->did10 = 0;
	k->have_up = 0;
	k->t_down = now;
	k->t_up = 0;
}

/* Milliseconds until kd_tick has something to do, or -1 when nothing is pending (poll with an infinite timeout). */
static int kd_timeout(const struct kd *k, unsigned int now)
{
	unsigned int held, next;

	if (!k->down || k->ignore)
		return -1;
	held = now - k->t_down;
	if (held & 0x80000000u)
		return 0;
	if (!k->did5)
		next = KD_HOLD5_MS;
	else if (!k->did10)
		next = KD_REBOOT_MS;
	else
		next = KD_STUCK_MS;
	return held >= next ? 0 : (int)(next - held);
}

/* Timer check; call after every poll wake-up (before handling an event read in the same wake-up). */
static enum kd_action kd_tick(struct kd *k, unsigned int now)
{
	unsigned int held;

	if (!k->down || k->ignore)
		return KD_NONE;
	held = now - k->t_down;
	if (held & 0x80000000u) {
		k->ignore = 1;
		return KD_NONE;
	}
	if (held >= KD_STUCK_MS) {
		k->ignore = 1;
		return KD_NONE;
	}
	if (!k->did5) {
		if (held < KD_HOLD5_MS)
			return KD_NONE;
		if (held >= KD_HOLD5_MS + KD_LATE_MS) {
			k->ignore = 1;
			return KD_NONE;
		}
		k->did5 = 1;
		return KD_HOLD5;
	}
	if (!k->did10) {
		if (held < KD_REBOOT_MS)
			return KD_NONE;
		if (held >= KD_REBOOT_MS + KD_LATE_MS) {
			k->ignore = 1;
			return KD_NONE;
		}
		k->did10 = 1;
		return KD_REBOOT;
	}
	return KD_NONE;
}

/* One KEY_POWER event value: 0 release, 1 press, 2 auto-repeat. */
static enum kd_action kd_event(struct kd *k, int value, unsigned int now)
{
	unsigned int held;

	if (value == 1) {
		if (k->down)
			return KD_NONE;
		k->down = 1;
		k->did5 = k->did10 = 0;
		k->t_down = now;
		k->ignore = k->have_up && (now - k->t_up) < KD_DEBOUNCE_MS;
		return KD_NONE;
	}
	if (value != 0 || !k->down)
		return KD_NONE;
	k->down = 0;
	k->have_up = 1;
	k->t_up = now;
	if (k->ignore) {
		k->ignore = 0;
		return KD_NONE;
	}
	held = now - k->t_down;
	if (held & 0x80000000u || held < KD_DEBOUNCE_MS)
		return KD_NONE;
	if (held >= KD_HOLD5_MS)
		return held < KD_REBOOT_MS && !k->did10 ? KD_POWEROFF : KD_NONE;
	if (held < KD_SHORT_MAX_MS)
		return KD_SHORT;
	if (held >= KD_LONG_MIN_MS && held < KD_HOLD5_MS)
		return KD_LONG;
	return KD_NONE;
}

#endif
