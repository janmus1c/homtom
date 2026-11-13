/* norwrite: stage R1 of docs/NOR_PLAN.md, the no-erase black-logo patch of the GO 630 NOR (EON EN29LV400BB,
 * 512 KiB, x16, nGCS0 at physical 0), from Linux 6.1 via /dev/mem.
 *
 *   norwrite --dry-run                 print the plan (counts, ranges, hashes). No device access.
 *   norwrite --plan-hash               print the SHA-256 of the compiled-in plan. No device access.
 *   norwrite --check                   read-only: hash the chip and report stock, patched, partial or foreign.
 *   norwrite --reset                   send only F0 (twice, with a settle check), then report as --check.
 *   norwrite --write R1-BLACK-LOGO     program the plan if the chip is stock (or already patched: no writes).
 *   norwrite --resume R1-BLACK-LOGO    as --write, and also accept a partial R1 state left by an interruption.
 *
 * The plan (norwrite_plan.h) is 1,669 (word, stock, target) entries generated offline from the d48b41 dump.
 * Every target only clears bits of its stock word, and only pixel value bytes of record 0 change.
 *
 * Bus access goes through read16() and write16() and time through now_us(); everything else is shared by the
 * device build and the host harness (workspace/nor-model/harness), which compiles this file unchanged with
 * NORWRITE_HOST defined and supplies the bus from a chip model.
 *
 * write16() has two callers: cmd(), which stores only the pairs (555,AA) (2AA,55) (555,90) (555,A0) (800,F0),
 * and prog(), which stores only plan entry i's target at plan entry i's word, after checking that the target
 * clears bits only, relative to both the stock word and the word just read. There is no erase, unlock-bypass,
 * CFI or protect command in the default build. The erase path for the R2/R3 analysis is behind
 * NORWRITE_ERASE_EXPERIMENT, which refuses to compile for the device and is never set by build.sh.
 *
 * Reset goes to word 0x800 (byte 0x1000): the datasheet makes the reset address a don't-care, and 0x800 is an
 * erased word inside protected SA0. If a killed earlier run left the chip in program setup and the chip took
 * the F0 as program data, it would land there, not on the reset vector at word 0.
 *
 * Order: plan self-check; single-instance lock /tmp/norwrite.lock and a clock check (every mode with bus access);
 * whole-chip SHA-256 gate; F0, settle, F0, settle (clears a half-sent command sequence
 * left by a killed run); autoselect ID, SA5 and SA0 protect check, F0, whole-chip re-hash; per word: skip if done, else program with toggle-bit (DQ6) polling, DQ5 handling and a timeout,
 * then read back; final whole-chip SHA-256 must equal the R1 target. Any failure sends F0 and stops without
 * retrying. Exit codes: 0 done or already patched, 2 usage, 3 stopped after bus writes (keep the unit powered,
 * do not reboot, dump and analyse), 4 refused before any program cycle, 5 partial R1 state without --resume,
 * 6 SA0 not protected (refused before any program cycle), 7 another copy holds the lock (no bus access). */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#include "norwrite_plan.h"

#define NOR_WORDS 0x40000u
#define SA5_WORD 0x10000u
#define RESET_WORD 0x00800u	/* erased word in protected SA0 */
#define PROG_TIMEOUT_US 1000u	/* datasheet maximum word program time is 300 us */
#define HARD_TIMEOUT_S 30
#define CONFIRM "R1-BLACK-LOGO"
#define LOCK_PATH "/tmp/norwrite.lock"

#define EX_OK 0
#define EX_USAGE 2
#define EX_FAIL 3
#define EX_REFUSED 4
#define EX_PARTIAL 5
#define EX_SA0 6
#define EX_LOCKED 7

#define ST_STOCK 0
#define ST_PATCHED 1
#define ST_PARTIAL 2
#define ST_FOREIGN 3

static volatile int lock_held;	/* also read by the SIGALRM handler */
static int clock_bad;

/* ---- platform: bus, timer, output, lock file ------------------------------------------------------- */
#ifdef NORWRITE_HOST
u16 nw_host_read16(u32 word);
void nw_host_write16(u32 word, u16 val);
u32 nw_host_now_us(void);
void nw_host_out(const char *s, long n);
long (*nw_host_lock)(int op, u32 pid, char *buf, long n);	/* harness lock file and pid table; NULL: always free */

static u32 lk_self(void)
{
	return nw_host_lock ? (u32)nw_host_lock(0, 0, 0, 0) : 1;
}

static long lk_create(const char *s, long n)
{
	return nw_host_lock ? nw_host_lock(1, 0, (char *)s, n) : 0;
}

static long lk_read(char *b, long n)
{
	return nw_host_lock ? nw_host_lock(2, 0, b, n) : -2;
}

static int lk_alive(u32 pid)
{
	return nw_host_lock ? (int)nw_host_lock(3, pid, 0, 0) : 1;
}

static void lk_unlink(void)
{
	if (nw_host_lock)
		nw_host_lock(4, 0, 0, 0);
}

static inline u16 read16(u32 word)
{
	return nw_host_read16(word);
}

static inline void write16(u32 word, u16 val)
{
	nw_host_write16(word, val);
}

static u32 now_us(void)
{
	return nw_host_now_us();
}

static void out(const char *s, long n)
{
	nw_host_out(s, n);
}

static int platform_map(int writable)
{
	(void)writable;
	return 0;
}

static void platform_lock(void)
{
}

static void platform_unlock(void)
{
}
#else
#ifdef NORWRITE_ERASE_EXPERIMENT
#error "NORWRITE_ERASE_EXPERIMENT is host-only"
#endif
static volatile u16 *nor;
static u32 sigsave[2];

static long sc(long n, long a, long b, long c, long d, long e, long f)
{
	register long r7 __asm__("r7") = n;
	register long r0 __asm__("r0") = a;
	register long r1 __asm__("r1") = b;
	register long r2 __asm__("r2") = c;
	register long r3 __asm__("r3") = d;
	register long r4 __asm__("r4") = e;
	register long r5 __asm__("r5") = f;

	__asm__ volatile("svc 0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5) : "memory");
	return r0;
}

static int bad(long r)
{
	return (unsigned long)r > (unsigned long)-4096;
}

static inline u16 read16(u32 word)
{
	return nor[word];
}

static inline void write16(u32 word, u16 val)
{
	nor[word] = val;
	__asm__ volatile("" ::: "memory");
}

static u32 now_us(void)
{
	long ts[2] = { 0, 0 };

	if (bad(sc(263, 1, (long)ts, 0, 0, 0, 0)))	/* clock_gettime(CLOCK_MONOTONIC) */
		clock_bad = 1;
	return (u32)ts[0] * 1000000u + (u32)ts[1] / 1000u;
}

static void out(const char *s, long n)
{
	sc(4, 1, (long)s, n, 0, 0, 0);
}

static u32 lk_self(void)
{
	return (u32)sc(20, 0, 0, 0, 0, 0, 0);	/* getpid */
}

static long lk_create(const char *s, long n)
{
	long fd = sc(5, (long)LOCK_PATH, 01 | 0100 | 0200, 0644, 0, 0, 0);	/* O_WRONLY | O_CREAT | O_EXCL */
	long r;

	if (bad(fd))
		return fd;
	r = sc(4, fd, (long)s, n, 0, 0, 0);
	sc(6, fd, 0, 0, 0, 0, 0);
	if (r != n) {
		sc(10, (long)LOCK_PATH, 0, 0, 0, 0, 0);
		return -5;
	}
	return 0;
}

static long lk_read(char *b, long n)
{
	long fd = sc(5, (long)LOCK_PATH, 0, 0, 0, 0, 0), r;

	if (bad(fd))
		return fd;
	r = sc(3, fd, (long)b, n, 0, 0, 0);
	sc(6, fd, 0, 0, 0, 0, 0);
	return r;
}

static int lk_alive(u32 pid)
{
	return sc(37, (long)pid, 0, 0, 0, 0, 0) != -3;	/* kill(pid, 0): only ESRCH means gone */
}

static void lk_unlink(void)
{
	sc(10, (long)LOCK_PATH, 0, 0, 0, 0, 0);
}

static void on_alarm(int sig)	/* the 30 s hard limit: drop the lock, then terminate as the default action did */
{
	(void)sig;
	if (lock_held)
		lk_unlink();
	out("STOP: 30 s hard timeout\n", 24);
	sc(1, EX_FAIL, 0, 0, 0, 0, 0);
}

static int platform_map(int writable)
{
	long fd = sc(5, (long)"/dev/mem", (writable ? 2 : 0) | 0x101000, 0, 0, 0, 0);	/* O_SYNC */
	long m;

	if (bad(fd))
		return -1;
	m = sc(192, 0, NOR_WORDS * 2, writable ? 3 : 1, 1, fd, 0);	/* mmap2 MAP_SHARED at physical page 0 */
	if (bad(m))
		return -1;
	nor = (volatile u16 *)m;
	return 0;
}

static void platform_lock(void)
{
	long it[4] = { 0, 0, HARD_TIMEOUT_S, 0 };
	u32 block[2] = { ~(1u << 13), 0xffffffffu };	/* everything but SIGALRM (14) */
	u32 act[5] = { (u32)on_alarm, 0, 0, 0xffffffffu, 0xffffffffu };	/* handler, flags, restorer, mask */

	sc(174, 14, (long)act, 0, 8, 0, 0);	/* rt_sigaction(SIGALRM) */
	sc(104, 0, (long)it, 0, 0, 0, 0);	/* setitimer(ITIMER_REAL): SIGALRM ends the run through on_alarm */
	sc(175, 0, (long)block, (long)sigsave, 8, 0, 0);	/* rt_sigprocmask(SIG_BLOCK) */
}

static void platform_unlock(void)
{
	sc(175, 2, (long)sigsave, 0, 8, 0, 0);	/* SIG_SETMASK */
}
#endif

/* ---- output ------------------------------------------------------------------------------------------ */
static void put(const char *s)
{
	long n = 0;

	while (s[n])
		n++;
	out(s, n);
}

static void hex(u32 v, int digits)
{
	char b[8];
	int i;

	for (i = 0; i < digits; i++)
		b[i] = "0123456789abcdef"[(v >> (4 * (digits - 1 - i))) & 15];
	out(b, digits);
}

static void dec(u32 v)
{
	char b[10];
	int i = 10;

	do {
		u32 q = 0, r = v;

		while (r >= 10) {	/* no division helper in a freestanding ARMv4T build */
			r -= 10;
			q++;
		}
		b[--i] = (char)('0' + r);
		v = q;
	} while (v);
	out(b + i, 10 - i);
}

static void kvhex(const char *k, u32 v, int digits)
{
	put(k);
	hex(v, digits);
	put("\n");
}

static void kvdec(const char *k, u32 v)
{
	put(k);
	dec(v);
	put("\n");
}

static void digest(const char *k, const u8 *d)
{
	int i;

	put(k);
	for (i = 0; i < 32; i++)
		hex(d[i], 2);
	put("\n");
}

static int same(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

/* ---- SHA-256 ----------------------------------------------------------------------------------------- */
struct sha {
	u32 h[8];
	u8 buf[64];
	u32 n;
	u32 len;
};

static const u32 sha_k[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static const u32 sha_iv[8] = {
	0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

static u32 ror(u32 x, int n)
{
	return (x >> n) | (x << (32 - n));
}

static void sha_block(struct sha *s)
{
	u32 w[64], a, b, c, d, e, f, g, h, t1, t2;
	int i;

	for (i = 0; i < 16; i++)
		w[i] = (u32)s->buf[4 * i] << 24 | (u32)s->buf[4 * i + 1] << 16 | (u32)s->buf[4 * i + 2] << 8 | s->buf[4 * i + 3];
	for (i = 16; i < 64; i++)
		w[i] = (ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10)) + w[i - 7] +
		       (ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 16];
	a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3];
	e = s->h[4]; f = s->h[5]; g = s->h[6]; h = s->h[7];
	for (i = 0; i < 64; i++) {
		t1 = h + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + sha_k[i] + w[i];
		t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
		h = g; g = f; f = e; e = d + t1;
		d = c; c = b; b = a; a = t1 + t2;
	}
	s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d;
	s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

static void sha_init(struct sha *s)
{
	int i;

	for (i = 0; i < 8; i++)
		s->h[i] = sha_iv[i];
	s->n = 0;
	s->len = 0;
}

static void sha_byte(struct sha *s, u8 b)
{
	s->buf[s->n++] = b;
	s->len++;
	if (s->n == 64) {
		sha_block(s);
		s->n = 0;
	}
}

static void sha_word(struct sha *s, u16 v)	/* the chip's byte order: low byte first */
{
	sha_byte(s, (u8)v);
	sha_byte(s, (u8)(v >> 8));
}

static void sha_final(struct sha *s, u8 *out32)
{
	u32 bits_hi = s->len >> 29, bits_lo = s->len << 3;
	int i;

	s->buf[s->n++] = 0x80;
	if (s->n > 56) {
		while (s->n < 64)
			s->buf[s->n++] = 0;
		sha_block(s);
		s->n = 0;
	}
	while (s->n < 56)
		s->buf[s->n++] = 0;
	for (i = 0; i < 4; i++) {
		s->buf[56 + i] = (u8)(bits_hi >> (24 - 8 * i));
		s->buf[60 + i] = (u8)(bits_lo >> (24 - 8 * i));
	}
	sha_block(s);
	for (i = 0; i < 32; i++)
		out32[i] = (u8)(s->h[i / 4] >> (24 - 8 * (i % 4)));
}

static int eq32(const u8 *a, const u8 *b)
{
	int i;

	for (i = 0; i < 32; i++)
		if (a[i] != b[i])
			return 0;
	return 1;
}

/* ---- plan self-check (no device access) --------------------------------------------------------------- */
static int plan_ok(u8 *plan_sha)
{
	struct sha s;
	u32 i, prev = 0;

	sha_init(&s);
	for (i = 0; i < NW_PLAN_N; i++) {
		u32 w = nw_plan_word[i];
		u16 st = nw_plan_stock[i], t = nw_plan_target[i];
		int k;

		if (w < NW_WORD_LO || w > NW_WORD_HI || (i && w <= prev) || (t & (u16)~st) || t == st)
			return 0;
		for (k = 0; k < 2; k++)
			if (((st >> (8 * k)) & 0xff) != ((t >> (8 * k)) & 0xff) &&
			    (2 * w + k < NW_BYTE_LO || 2 * w + k > NW_BYTE_HI))
				return 0;
		prev = w;
		for (k = 0; k < 4; k++)
			sha_byte(&s, (u8)(w >> (8 * k)));
		sha_word(&s, t);
	}
	sha_final(&s, plan_sha);
	return eq32(plan_sha, nw_sha_plan);
}

/* ---- bus writes: the only two callers of write16() ---------------------------------------------------- */
static const u32 cmd_allowed[][2] = {
	{ 0x555, 0xaa }, { 0x2aa, 0x55 }, { 0x555, 0x90 }, { 0x555, 0xa0 }, { RESET_WORD, 0xf0 }
};

static int cmd(u32 word, u32 val)
{
	u32 i;

	for (i = 0; i < sizeof(cmd_allowed) / sizeof(cmd_allowed[0]); i++)
		if (cmd_allowed[i][0] == word && cmd_allowed[i][1] == val) {
			write16(word, (u16)val);
			return 0;
		}
	return -1;
}

static int prog(u32 i, u16 cur)
{
	u32 w;
	u16 t;

	if (i >= NW_PLAN_N)
		return -1;
	w = nw_plan_word[i];
	t = nw_plan_target[i];
	if (w < NW_WORD_LO || w > NW_WORD_HI || (t & (u16)~cur) || (t & (u16)~nw_plan_stock[i]) || t == cur)
		return -1;
	cmd(0x555, 0xaa);
	cmd(0x2aa, 0x55);
	cmd(0x555, 0xa0);
	write16(w, t);
	return 0;
}

/* ---- whole-chip scan ---------------------------------------------------------------------------------- */
/* One pass: raw = SHA-256 of the chip; norm = SHA-256 with every plan word replaced by its stock value, which
 * is only allowed when the word lies between target and stock bit-wise (target <= word <= stock). */
static int scan(u8 *raw, u8 *norm)
{
	struct sha a, b;
	u32 w, i = 0;
	int interval_ok = 1, plan_is_stock = 1;

	sha_init(&a);
	sha_init(&b);
	for (w = 0; w < NOR_WORDS; w++) {
		u16 v = read16(w);

		sha_word(&a, v);
		if (i < NW_PLAN_N && nw_plan_word[i] == w) {
			if ((v & (u16)~nw_plan_stock[i]) || (nw_plan_target[i] & (u16)~v))
				interval_ok = 0;
			if (v != nw_plan_stock[i])
				plan_is_stock = 0;
			sha_word(&b, nw_plan_stock[i]);
			i++;
		} else {
			sha_word(&b, v);
		}
	}
	sha_final(&a, raw);
	sha_final(&b, norm);
	if (eq32(raw, nw_sha_target))
		return ST_PATCHED;
	if (eq32(raw, nw_sha_stock) && plan_is_stock && eq32(norm, nw_sha_stock))
		return ST_STOCK;
	if (interval_ok && eq32(norm, nw_sha_stock))
		return ST_PARTIAL;
	return ST_FOREIGN;
}

static const char *state_name(int st)
{
	return st == ST_STOCK ? "stock" : st == ST_PATCHED ? "patched" : st == ST_PARTIAL ? "partial" : "foreign";
}

/* ---- program status polling (datasheet flowchart 6: toggle bit with the DQ5 re-check) ----------------- */
#define W_OK 0
#define W_DQ5 1
#define W_TIMEOUT 2

static int wait_done(u32 word)
{
	u32 start = now_us();
	u16 a, b;

	for (;;) {
		a = read16(word);
		b = read16(word);
		if (!((a ^ b) & 0x40))
			return W_OK;
		if (b & 0x20) {
			a = read16(word);
			b = read16(word);
			return (a ^ b) & 0x40 ? W_DQ5 : W_OK;
		}
		if (now_us() - start > PROG_TIMEOUT_US) {	/* re-check once: the process may have been preempted */
			a = read16(word);
			b = read16(word);
			return (a ^ b) & 0x40 ? W_TIMEOUT : W_OK;
		}
	}
}

static int settle(void)	/* wait until reads of RESET_WORD stop toggling */
{
	u32 start = now_us();

	while (now_us() - start <= PROG_TIMEOUT_US)
		if (!((read16(RESET_WORD) ^ read16(RESET_WORD)) & 0x40))
			return 1;
	return 0;
}

static int reset_chip(void)
{
	cmd(RESET_WORD, 0xf0);
	if (!settle())
		return 0;
	cmd(RESET_WORD, 0xf0);
	return settle();
}

static int read_mode_ok(u32 word)
{
	u16 a = read16(word), b = read16(word);

	return a == b;
}

static int stop(const char *why, u32 i, u32 word, u16 cur, u16 got)
{
	cmd(RESET_WORD, 0xf0);
	put("STOP: ");
	put(why);
	put("\n");
	kvdec("plan index ", i);
	kvhex("word ", word, 5);
	kvhex("before ", cur, 4);
	kvhex("read ", got, 4);
	kvhex("want ", i < NW_PLAN_N ? nw_plan_target[i] : 0, 4);
	put(read_mode_ok(word) ? "chip in read mode after F0\n" : "chip still toggling after F0\n");
	put("keep the unit powered, do not reboot; dump with norid2 and analyse before any rerun\n");
	return EX_FAIL;
}

/* ---- R1 ----------------------------------------------------------------------------------------------- */
static int run_r1(int resume)
{
	u8 raw[32], norm[32], raw2[32];
	u16 id0, id100, id1, prot0, prot5;
	u32 i, done = 0, skipped = 0, t0;
	int st, rc = EX_OK;

	if (platform_map(1)) {
		put("cannot map /dev/mem\n");
		return EX_REFUSED;
	}
	st = scan(raw, norm);
	digest("sha256_before ", raw);
	put("state ");
	put(state_name(st));
	put("\n");
	if (st == ST_PATCHED) {
		put("already patched: no bus writes\n");
		return EX_OK;
	}
	if (st == ST_FOREIGN) {
		put("chip matches neither stock, patched nor a partial R1 state: refusing, no bus writes\n");
		return EX_REFUSED;
	}
	if (st == ST_PARTIAL && !resume) {
		put("partial R1 state: rerun with --resume to complete it; no bus writes\n");
		return EX_PARTIAL;
	}

	platform_lock();
	t0 = now_us();
	if (!reset_chip()) {
		platform_unlock();
		put("STOP: chip still busy after reset\n");
		return EX_FAIL;
	}
	cmd(0x555, 0xaa);
	cmd(0x2aa, 0x55);
	cmd(0x555, 0x90);
	id0 = read16(0x000);
	id100 = read16(0x100);
	id1 = read16(0x001);
	prot0 = read16(0x002);
	prot5 = read16(SA5_WORD + 2);
	cmd(RESET_WORD, 0xf0);
	kvhex("mfr ", id0, 4);
	kvhex("mfr_bank1 ", id100, 4);
	kvhex("dev ", id1, 4);
	kvhex("protect_sa0 ", prot0, 4);
	kvhex("protect_sa5 ", prot5, 4);
	if (scan(raw2, norm) != st || !eq32(raw, raw2)) {
		platform_unlock();
		put("STOP: array differs after autoselect and reset\n");
		put("keep the unit powered, do not reboot; dump with norid2 and analyse before any rerun\n");
		return EX_FAIL;
	}
	if ((id0 & 0xff) != 0x7f || (id100 & 0xff) != 0x1c || id1 != 0x22ba) {
		platform_unlock();
		put("not an EON EN29LV400 bottom boot (7F/1C, 22BA): refusing, array verified unchanged\n");
		return EX_REFUSED;
	}
	if ((prot5 & 0xff) != 0) {
		platform_unlock();
		put("SA5 reports sector protection: refusing, array verified unchanged\n");
		return EX_REFUSED;
	}
	if ((prot0 & 0xff) != 0x01) {
		platform_unlock();
		put("SA0 does not report sector protection (the reset word 0x800 relies on it): refusing, array verified unchanged\n");
		return EX_SA0;
	}

	for (i = 0; i < NW_PLAN_N; i++) {
		u32 w = nw_plan_word[i];
		u16 t = nw_plan_target[i], cur = read16(w), got;
		int r;

		if (cur == t) {
			skipped++;
			continue;
		}
		if (cur & (u16)~nw_plan_stock[i]) {
			rc = stop("word has bits set that the stock word does not have", i, w, cur, cur);
			break;
		}
		if (t & (u16)~cur) {
			rc = stop("target needs a 0->1 change", i, w, cur, cur);
			break;
		}
		if (prog(i, cur)) {
			rc = stop("prog() refused the entry", i, w, cur, cur);
			break;
		}
		r = wait_done(w);
		got = read16(w);
		if (r != W_OK) {
			rc = stop(r == W_DQ5 ? "DQ5 (exceeded timing limits)" : "program timeout", i, w, cur, got);
			break;
		}
		if (got != t) {
			rc = stop("read-back differs from target", i, w, cur, got);
			break;
		}
		done++;
	}
	kvdec("programmed ", done);
	kvdec("already_done ", skipped);
	if (rc != EX_OK) {
		platform_unlock();
		return rc;
	}
	st = scan(raw, norm);
	kvdec("elapsed_ms ", (now_us() - t0) / 1000u);
	platform_unlock();
	digest("sha256_after ", raw);
	if (st != ST_PATCHED) {
		put("STOP: whole chip does not match the R1 target\n");
		put("keep the unit powered, do not reboot; dump with norid2 and analyse before any rerun\n");
		return EX_FAIL;
	}
	put("R1 complete: whole chip matches the target\n");
	return EX_OK;
}

static int check(int reset)
{
	u8 raw[32], norm[32];
	int st;

	if (platform_map(reset)) {
		put("cannot map /dev/mem\n");
		return EX_REFUSED;
	}
	if (reset) {
		platform_lock();
		st = reset_chip();
		platform_unlock();
		put(st ? "reset sent, chip settled\n" : "reset sent, chip STILL BUSY\n");
	}
	st = scan(raw, norm);
	digest("sha256 ", raw);
	put("state ");
	put(state_name(st));
	put("\n");
	return st == ST_FOREIGN ? EX_REFUSED : st == ST_PARTIAL ? EX_PARTIAL : EX_OK;
}

/* ---- single-instance lock: taken before the first bus access, dropped on every return -------------------- */
/* The file holds the owner's pid in decimal. A pid that no longer exists (or is our own) marks a stale lock
 * left by a killed run; it is removed once and the create retried. Unreadable or empty content counts as held. */
static int lock_take(void)
{
	char me[12], b[12];
	u32 self = lk_self(), v = self, pid;
	long n = sizeof(me), r, k;
	int tries;

	do
		me[--n] = (char)('0' + v % 10u);
	while ((v /= 10u) && n);
	for (tries = 0; tries < 2; tries++) {
		r = lk_create(me + n, (long)sizeof(me) - n);
		if (!r) {
			lock_held = 1;
			return 0;
		}
		if (r != -17)	/* EEXIST */
			break;
		r = lk_read(b, sizeof(b));
		for (pid = 0, k = 0; k < r && b[k] >= '0' && b[k] <= '9'; k++)
			pid = pid * 10u + (u32)(b[k] - '0');
		if (r <= 0 || k != r || !pid || (pid != self && lk_alive(pid))) {
			put("another norwrite holds " LOCK_PATH " (pid ");
			dec(pid);
			put(", 0 = unreadable): refusing, no bus access\n");
			return EX_LOCKED;
		}
		put("removing stale " LOCK_PATH " of pid ");
		dec(pid);
		put("\n");
		lk_unlink();
	}
	put("cannot create " LOCK_PATH ": refusing, no bus access\n");
	return EX_LOCKED;
}

static void lock_drop(void)
{
	if (lock_held) {
		lock_held = 0;
		lk_unlink();
	}
}

static int locked(int write, int arg)	/* every mode with bus access runs inside this */
{
	int rc = lock_take();

	if (rc)
		return rc;
	now_us();
	if (clock_bad) {
		put("clock_gettime failed: refusing, no bus access\n");
		rc = EX_REFUSED;
	} else {
		rc = write ? run_r1(arg) : check(arg);
	}
	lock_drop();
	return rc;
}

static void dry_run(const u8 *plan_sha)
{
	u32 i, bytes = 0, bits = 0;

	for (i = 0; i < NW_PLAN_N; i++) {
		u16 c = nw_plan_stock[i] & (u16)~nw_plan_target[i];

		bytes += ((c & 0xff) != 0) + ((c >> 8) != 0);
		while (c) {
			bits += c & 1;
			c >>= 1;
		}
	}
	put("R1 black-logo plan (no device access)\n");
	kvdec("words ", NW_PLAN_N);
	put("word_range ");
	hex(NW_WORD_LO, 5);
	put("-");
	hex(NW_WORD_HI, 5);
	put("\nbyte_allowlist ");
	hex(NW_BYTE_LO, 5);
	put("-");
	hex(NW_BYTE_HI, 5);
	put("\n");
	kvdec("bytes_cleared ", bytes);
	kvdec("bits_cleared ", bits);
	put("commands AA@555 55@2AA 90@555 A0@555 F0@800; no erase\n");
	digest("plan_sha256 ", plan_sha);
	digest("stock_sha256 ", nw_sha_stock);
	digest("target_sha256 ", nw_sha_target);
}

#ifndef NORWRITE_HOST
static
#endif
int norwrite_main(int argc, char **argv)
{
	u8 plan_sha[32];

	if (!plan_ok(plan_sha)) {
		put("compiled-in plan fails its self-check: refusing\n");
		return EX_REFUSED;
	}
	if (argc == 2 && same(argv[1], "--dry-run")) {
		dry_run(plan_sha);
		return EX_OK;
	}
	if (argc == 2 && same(argv[1], "--plan-hash")) {
		digest("", plan_sha);
		return EX_OK;
	}
	if (argc == 2 && same(argv[1], "--check"))
		return locked(0, 0);
	if (argc == 2 && same(argv[1], "--reset"))
		return locked(0, 1);
	if (argc == 3 && same(argv[1], "--write") && same(argv[2], CONFIRM))
		return locked(1, 0);
	if (argc == 3 && same(argv[1], "--resume") && same(argv[2], CONFIRM))
		return locked(1, 1);
	put("usage: norwrite --dry-run | --plan-hash | --check | --reset | --write " CONFIRM " | --resume " CONFIRM "\n");
	return EX_USAGE;
}

#ifdef NORWRITE_ERASE_EXPERIMENT
/* Host-only analysis of the R2/R3 sector-erase design (docs/NOR_PLAN.md section 10). Not a device path:
 * this block refuses to compile without NORWRITE_HOST, and build.sh never defines it. */
#ifndef NW_ERASE_SECTORS
#define NW_ERASE_SECTORS 6, 5
#endif
#define ERASE_TIMEOUT_US 12000000u	/* datasheet maximum sector erase time is 10 s */

static const u32 nw_erase_allow[] = { NW_ERASE_SECTORS };
static const u32 nw_sector_word[11] = {
	0x00000, 0x02000, 0x03000, 0x04000, 0x08000, 0x10000, 0x18000, 0x20000, 0x28000, 0x30000, 0x38000
};

static int exp_wait(u32 word, u32 timeout_us)
{
	u32 start = now_us();
	u16 a, b;

	for (;;) {
		a = read16(word);
		b = read16(word);
		if (!((a ^ b) & 0x40))
			return W_OK;
		if (b & 0x20) {
			a = read16(word);
			b = read16(word);
			return (a ^ b) & 0x40 ? W_DQ5 : W_OK;
		}
		if (now_us() - start > timeout_us) {
			a = read16(word);
			b = read16(word);
			return (a ^ b) & 0x40 ? W_TIMEOUT : W_OK;
		}
	}
}

int norwrite_exp_erase(u32 sector)
{
	u32 i, ok = 0, base;

	for (i = 0; i < sizeof(nw_erase_allow) / sizeof(nw_erase_allow[0]); i++)
		if (nw_erase_allow[i] == sector)
			ok = 1;
	if (!ok || sector >= 11)
		return -1;
	base = nw_sector_word[sector];
	write16(0x555, 0xaa);
	write16(0x2aa, 0x55);
	write16(0x555, 0x80);
	write16(0x555, 0xaa);
	write16(0x2aa, 0x55);
	write16(base, 0x30);
	if (exp_wait(base, ERASE_TIMEOUT_US) != W_OK) {
		write16(0x000, 0xf0);
		return -2;
	}
	for (i = 0; i < (sector + 1 < 11 ? nw_sector_word[sector + 1] : NOR_WORDS) - base; i++)
		if (read16(base + i) != 0xffff)
			return -3;
	return 0;
}

int norwrite_exp_program(u32 word, u16 val)
{
	u32 s, ok = 0, i;
	u16 cur = read16(word);

	for (s = 0; s < 11; s++)
		if (word >= nw_sector_word[s] && word < (s + 1 < 11 ? nw_sector_word[s + 1] : NOR_WORDS))
			for (i = 0; i < sizeof(nw_erase_allow) / sizeof(nw_erase_allow[0]); i++)
				if (nw_erase_allow[i] == s)
					ok = 1;
	if (!ok || (val & (u16)~cur))
		return -1;
	if (cur == val)
		return 0;
	write16(0x555, 0xaa);
	write16(0x2aa, 0x55);
	write16(0x555, 0xa0);
	write16(word, val);
	if (exp_wait(word, PROG_TIMEOUT_US) != W_OK || read16(word) != val) {
		write16(0x000, 0xf0);
		return -2;
	}
	return 0;
}
#endif

#ifndef NORWRITE_HOST
int main_c(long *sp)
{
	return norwrite_main((int)sp[0], (char **)(sp + 1));
}

__asm__(".globl _start\n_start:\n mov r0, sp\n bl main_c\n mov r7, #1\n svc 0\n");
#endif
