/* Fault-injection harness for tools/devtools/norwrite.c. The writer source is included unchanged with
 * NORWRITE_HOST defined; its read16/write16/now_us land in the C chip model (nor_model.c) through the
 * nw_host_* functions below. A bus monitor fails the run on any write that is not part of the R1 protocol.
 *
 *   harness r1 STOCK.bin OUTDIR [seed]       R1 suite: clean run, rerun, cuts, faults; writes transcripts
 *                                          and snapshots for run_harness.py
 *   harness fuzz STOCK.bin OUT.tr seed n     random bus traffic transcript for the C/Python model diff
 *   harness erase STOCK.bin HOMTOM.bin OUTDIR  (built with NORWRITE_ERASE_EXPERIMENT) R2/R3 analysis
 * Use run_harness.py, which builds, runs and checks everything. */
#define _GNU_SOURCE
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nor_model.h"

#define NORWRITE_HOST
#include "norwrite.c"

#define IMG_BYTES 0x80000u
#define REGION_LO 0x28000u
#define REGION_HI 0x40000u

static struct nor_model chip_store, *M = &chip_store;
static uint8_t stock[IMG_BYTES];
#ifdef NORWRITE_ERASE_EXPERIMENT
static uint8_t homtom[IMG_BYTES];
#endif
static int32_t plan_idx[NM_WORDS];

static long n_ops, n_writes, n_prog_cycles;
static long cut_at = -1;
static int cut_kind;	/* 1 power loss, 2 process killed (chip keeps running) */
static jmp_buf cut_jb;
static long flip_at = -1;
static uint16_t flip_mask;
static long jump_at = -1;
static double jump_us;
static long inject_prog = -1;
static int inject_kind;	/* 1 DQ5, 2 stuck busy, 3 slow beyond the writer timeout */
static int monitor_on = 1;
static int mon_state;
static long mon_failures, mon_erase_capable, mon_total_writes;
static FILE *tr;
static int quiet = 1;
static char outbuf[16384];
static size_t outlen;

/* run bookkeeping for cut placement */
static long first_prog_op, last_prog_op, id_op, total_ops;
static long *busy_ops;
static long n_busy_ops, cap_busy_ops;
static int record_busy;

static long failures;

#define FAIL(...) do { failures++; fprintf(stderr, "FAIL: " __VA_ARGS__); fputc('\n', stderr); } while (0)

/* ---- host bus ---------------------------------------------------------------------------------------- */
static void do_cut(void)
{
	if (cut_kind == 1)
		nm_power_loss(M);
	else
		nm_wait(M, 2e6);	/* killed process: the chip finishes whatever it was doing */
	longjmp(cut_jb, 1);
}

static void monitor(u32 w, u16 v)
{
	int i;

	mon_total_writes++;
	if (!monitor_on)
		return;
	if (mon_state != 4 && ((v & 0xff) == 0x80 || (v & 0xff) == 0x30 || (v & 0xff) == 0x10))
		mon_erase_capable++;	/* erase setup, sector erase or chip erase byte in a command slot */
	switch (mon_state) {
	case 0:
		if (w == 0x555 && v == 0xaa)
			mon_state = 1;
		else if (!(w == RESET_WORD && v == 0xf0))
			goto bad;
		return;
	case 1:
		if (w == 0x2aa && v == 0x55) {
			mon_state = 2;
			return;
		}
		goto bad;
	case 2:
		if (w == 0x555 && v == 0x90) {
			mon_state = 3;
			id_op = n_ops;
			return;
		}
		if (w == 0x555 && v == 0xa0) {
			mon_state = 4;
			return;
		}
		goto bad;
	case 3:
		if (w == RESET_WORD && v == 0xf0) {
			mon_state = 0;
			return;
		}
		goto bad;
	case 4:
		i = w < NM_WORDS ? plan_idx[w] : -1;
		if (i < 0 || v != nw_plan_target[i] || (v & (u16)~nw_plan_stock[i]))
			goto bad;
		n_prog_cycles++;
		if (!first_prog_op)
			first_prog_op = n_ops;
		last_prog_op = n_ops;
		mon_state = 0;
		return;
	}
bad:
	mon_failures++;
	FAIL("bus monitor: write %05x <- %04x in monitor state %d", w, v, mon_state);
	mon_state = 0;
}

u16 nw_host_read16(u32 w)
{
	u16 v;

	n_ops++;
	v = nm_read(M, w);
	if (tr)
		fprintf(tr, "R %x %x\n", w, v);
	if (record_busy && (M->state == NM_BUSY_PROG)) {
		if (n_busy_ops == cap_busy_ops) {
			cap_busy_ops = cap_busy_ops ? 2 * cap_busy_ops : 4096;
			busy_ops = realloc(busy_ops, cap_busy_ops * sizeof(long));
		}
		busy_ops[n_busy_ops++] = n_ops;
	}
	if (n_ops == flip_at)
		v ^= flip_mask;
	if (n_ops == cut_at)
		do_cut();
	return v;
}

void nw_host_write16(u32 w, u16 v)
{
	int data_cycle = monitor_on && mon_state == 4;

	n_ops++;
	n_writes++;
	monitor(w, v);
	if (data_cycle && n_prog_cycles - 1 == inject_prog) {
		if (inject_kind == 1)
			M->fail_next_program = 1;
		else if (inject_kind == 3)
			M->slow_next_program_us = 1500.0;
	}
	nm_write(M, w, v);
	if (data_cycle && n_prog_cycles - 1 == inject_prog && inject_kind == 2)
		M->stuck_busy = 1;
	if (tr)
		fprintf(tr, "W %x %x\n", w, v);
	if (n_ops == cut_at)
		do_cut();
}

/* Simulated /tmp/norwrite.lock and process table. Every run() is a new pid; a run ended by a cut is dead, so
 * the lock it leaves is stale. lkf_other is a concurrent copy that is alive. */
static int lkf_exists, lkf_nodir;
static char lkf_buf[16];
static long lkf_len, n_stale_removed;
static u32 pid_now = 1000, lkf_other;

static long host_lock(int op, u32 pid, char *buf, long n)
{
	switch (op) {
	case 0:
		return pid_now;
	case 1:
		if (lkf_nodir)
			return -2;
		if (lkf_exists)
			return -17;
		lkf_exists = 1;
		lkf_len = n < (long)sizeof(lkf_buf) ? n : (long)sizeof(lkf_buf);
		memcpy(lkf_buf, buf, lkf_len);
		return 0;
	case 2:
		if (!lkf_exists)
			return -2;
		n = n < lkf_len ? n : lkf_len;
		memcpy(buf, lkf_buf, n);
		return n;
	case 3:
		return pid == pid_now || (lkf_other && pid == lkf_other);
	case 4:
		lkf_exists = 0;
		return 0;
	}
	return -22;
}

static void lock_put(const char *s)	/* a lock file left by someone else */
{
	lkf_exists = 1;
	lkf_len = (long)strlen(s);
	memcpy(lkf_buf, s, lkf_len);
}

static u32 lock_owner(void)
{
	char b[17];

	memcpy(b, lkf_buf, lkf_len);
	b[lkf_len] = 0;
	return lkf_exists ? (u32)strtoul(b, NULL, 10) : 0;
}

u32 nw_host_now_us(void)
{
	if (jump_at >= 0 && n_ops >= jump_at) {
		nm_wait(M, jump_us);	/* the process was descheduled; the chip kept running */
		if (tr)
			fprintf(tr, "T %.17g\n", jump_us);
		jump_at = -1;
	}
	return (u32)M->now;
}

void nw_host_out(const char *s, long n)
{
	if (!quiet)
		fwrite(s, 1, n, stdout);
	if (outlen + n < sizeof(outbuf)) {
		memcpy(outbuf + outlen, s, n);
		outlen += n;
	}
}

/* ---- helpers ----------------------------------------------------------------------------------------- */
static int run(const char *a1, const char *a2)
{
	char *argv[3] = { "norwrite", (char *)a1, (char *)a2 };
	int rc;

	n_ops = n_writes = n_prog_cycles = 0;
	mon_state = 0;
	outlen = 0;
	pid_now++;
	lock_held = 0;
	if (setjmp(cut_jb)) {
		cut_at = -1;
		return -1;
	}
	rc = norwrite_main(a2 ? 3 : 2, argv);
	cut_at = -1;
	if (memmem(outbuf, outlen, "removing stale", 14))
		n_stale_removed++;
	if (lkf_exists && lock_owner() == pid_now)
		FAIL("%s: exit %d left its own lock file", a1, rc);
	return rc;
}

static int run_write(void)
{
	return run("--write", "R1-BLACK-LOGO");
}

static int run_resume(void)
{
	return run("--resume", "R1-BLACK-LOGO");
}

static void chip_sha(u8 *out32)
{
	struct sha s;
	uint32_t w;

	sha_init(&s);
	for (w = 0; w < NM_WORDS; w++)
		sha_word(&s, M->cells[w]);
	sha_final(&s, out32);
}

static int chip_is(const u8 *want)
{
	u8 d[32];

	chip_sha(d);
	return eq32(d, want);
}

static void fresh(void)
{
	nm_init(M, stock);
}

static uint64_t rs = 88172645463325252ull;

static uint64_t rnd(void)
{
	rs ^= rs << 13;
	rs ^= rs >> 7;
	rs ^= rs << 17;
	return rs;
}

static long rnd_range(long lo, long hi)	/* inclusive */
{
	return lo + (long)(rnd() % (uint64_t)(hi - lo + 1));
}

/* Invariant: words outside the plan equal stock; plan words lie between target and stock bit-wise. */
static int invariants(const char *ctx)
{
	uint32_t w;

	for (w = 0; w < NM_WORDS; w++) {
		int i = plan_idx[w];
		uint16_t c = M->cells[w], s = stock[2 * w] | stock[2 * w + 1] << 8;

		if (i < 0 ? c != s : ((c & ~s & 0xffff) || (nw_plan_target[i] & ~c & 0xffff))) {
			FAIL("%s: word %05x = %04x (stock %04x)", ctx, w, c, s);
			return 0;
		}
	}
	return 1;
}

/* Region snapshots, deduplicated, for the repo decoder check in run_harness.py. */
static FILE *snap;
static uint64_t *snap_hash;
static long n_snap, n_snap_states;

static void snapshot(void)
{
	uint8_t img[IMG_BYTES];
	uint64_t h = 1469598103934665603ull;
	uint32_t i;
	long k;

	nm_image(M, img);
	for (i = REGION_LO; i < REGION_HI; i++)
		h = (h ^ img[i]) * 1099511628211ull;
	n_snap_states++;
	for (k = 0; k < n_snap; k++)
		if (snap_hash[k] == h)
			return;
	snap_hash = realloc(snap_hash, (n_snap + 1) * sizeof(uint64_t));
	snap_hash[n_snap++] = h;
	fwrite(img + REGION_LO, 1, REGION_HI - REGION_LO, snap);
}

/* After a faulted run: check, snapshot, then converge and check idempotence. Recovery ladder, each step only if
 * the previous did not reach the target: --write (or --resume when it reports a partial state), then
 * --reset and again, then a power cycle and again. */
struct conv { long direct, resumed, reset, power_cycled, failed; };

static int nowrite_claim_ok(const char *ctx)
{
	if (memmem(outbuf, outlen, "no bus writes", 13) && n_writes) {
		FAIL("%s: run said no bus writes but made %ld", ctx, n_writes);
		return 0;
	}
	return 1;
}

static int attempt(const char *ctx, int *resumed)
{
	int rc = run_write();

	nowrite_claim_ok(ctx);
	if (rc == EX_PARTIAL) {
		rc = run_resume();
		nowrite_claim_ok(ctx);
		*resumed = 1;
	}
	return rc == EX_OK && chip_is(nw_sha_target);
}

static void converge(const char *ctx, struct conv *cv)
{
	int rc, resumed = 0;

	invariants(ctx);
	snapshot();
	M->fail_next_program = 0;
	M->slow_next_program_us = 0;
	M->silent_protect_mask = 0;
	inject_prog = -1;
	flip_at = -1;
	jump_at = -1;
	if (attempt(ctx, &resumed)) {
		resumed ? cv->resumed++ : cv->direct++;
	} else {
		run("--reset", NULL);
		invariants(ctx);
		if (attempt(ctx, &resumed)) {
			cv->reset++;
		} else {
			nm_power_loss(M);
			invariants(ctx);
			if (attempt(ctx, &resumed)) {
				cv->power_cycled++;
			} else {
				cv->failed++;
				FAIL("%s: did not converge\n%.*s", ctx, (int)outlen, outbuf);
				return;
			}
		}
	}
	invariants(ctx);
	rc = run_write();
	if (rc != EX_OK || n_writes || !chip_is(nw_sha_target))
		FAIL("%s: rerun after convergence not idempotent (rc %d, %ld writes)", ctx, rc, n_writes);
}

static void tally(long *hist, int rc)
{
	hist[rc < 0 ? 6 : rc > 5 ? 6 : rc]++;
}

static void print_hist(const char *name, long *hist)
{
	printf("  %-34s exit0 %ld, exit3 %ld, exit4 %ld, exit5 %ld, cut %ld\n", name, hist[0], hist[3], hist[4],
	       hist[5], hist[6]);
}

/* ---- R1 suite ---------------------------------------------------------------------------------------- */
static int suite_r1(const char *outdir)
{
	char path[1024];
	long i, hist[7];
	int rc;
	struct conv cv;
	long total_runs = 0;

	/* 0. no-bus modes */
	fresh();
	rc = run("--dry-run", NULL);
	if (rc != 0 || n_ops)
		FAIL("dry-run touched the bus or failed");
	rc = run("--plan-hash", NULL);
	if (rc != 0 || n_ops)
		FAIL("plan-hash touched the bus");
	rc = run("--write", "yes");
	if (rc != EX_USAGE || n_ops)
		FAIL("wrong confirm token was not refused before bus access");
	rc = run("--check", NULL);
	if (rc != 0 || n_writes)
		FAIL("--check wrote to the bus");

	/* 1. clean run with transcript */
	snprintf(path, sizeof(path), "%s/clean.tr", outdir);
	tr = fopen(path, "w");
	fprintf(tr, "# norwrite clean R1 run on d48b41\n");
	fresh();
	record_busy = 1;
	first_prog_op = last_prog_op = 0;
	rc = run_write();
	record_busy = 0;
	fclose(tr);
	tr = NULL;
	total_ops = n_ops;
	total_runs++;
	printf("clean run: exit %d, %ld bus ops, %ld writes, %ld program cycles, %.1f ms virtual\n", rc, n_ops,
	       n_writes, n_prog_cycles, M->now / 1000.0);
	if (rc != 0 || !chip_is(nw_sha_target) || n_prog_cycles != (long)NW_PLAN_N)
		FAIL("clean run did not reach the target");
	invariants("clean");
	snapshot();
	printf("  first program cycle at op %ld, last at op %ld, autoselect at op %ld, %ld ops with the chip busy\n",
	       first_prog_op, last_prog_op, id_op, n_busy_ops);

	/* 2. idempotent rerun */
	rc = run_write();
	total_runs++;
	printf("rerun on patched chip: exit %d, %ld bus writes, %ld bus reads\n", rc, n_writes, n_ops - n_writes);
	if (rc != 0 || n_writes)
		FAIL("rerun was not a no-op");
	rc = run("--check", NULL);
	if (rc != 0)
		FAIL("--check on patched chip");

	/* 3. power cuts and kills */
	{
		struct { const char *name; int kind; long n; int where; } plan[] = {
			{ "power cut, program phase (uniform)", 1, 1000, 0 },
			{ "power cut, mid-word (chip busy)", 1, 400, 1 },
			{ "power cut, preflight/ID/rehash", 1, 100, 2 },
			{ "power cut, around autoselect", 1, 24, 3 },
			{ "power cut, final hash", 1, 50, 4 },
			{ "process kill, program phase", 2, 300, 0 },
			{ "process kill, around autoselect", 2, 24, 3 },
		};
		unsigned k;

		printf("power cuts and kills (each followed by convergence and an idempotence rerun):\n");
		for (k = 0; k < sizeof(plan) / sizeof(plan[0]); k++) {
			memset(hist, 0, sizeof(hist));
			memset(&cv, 0, sizeof(cv));
			for (i = 0; i < plan[k].n; i++) {
				long at;

				switch (plan[k].where) {
				case 0: at = rnd_range(first_prog_op - 8, last_prog_op + 8); break;
				case 1: at = busy_ops[rnd_range(0, n_busy_ops - 1)]; break;
				case 2: at = rnd_range(1, first_prog_op - 9); break;
				case 3: at = id_op - 4 + i % 12 + (i >= 12 ? 0 : 0); break;
				default: at = rnd_range(last_prog_op + 20, total_ops - 1); break;
				}
				fresh();
				M->rng ^= (uint64_t)at * 0x9e3779b97f4a7c15ull;
				M->f0_resets_setup = (int)(i & 1);	/* both readings of the datasheet's reset rule */
				cut_at = at;
				cut_kind = plan[k].kind;
				rc = run_write();
				tally(hist, rc);
				converge(plan[k].name, &cv);
				total_runs += 3;
			}
			print_hist(plan[k].name, hist);
			printf("  %-34s converged: direct %ld, --resume %ld, --reset %ld, power cycle %ld, failed %ld\n", "",
			       cv.direct, cv.resumed, cv.reset, cv.power_cycled, cv.failed);
		}
	}

	/* 4. injected program faults */
	{
		struct { const char *name; int kind; long n; } plan[] = {
			{ "DQ5 on program k", 1, 120 },
			{ "stuck busy on program k", 2, 40 },
			{ "program slower than timeout (F0 while busy)", 3, 40 },
		};
		unsigned k;

		printf("injected program faults:\n");
		for (k = 0; k < sizeof(plan) / sizeof(plan[0]); k++) {
			long f0busy = 0, cycles_after = 0;

			memset(hist, 0, sizeof(hist));
			memset(&cv, 0, sizeof(cv));
			for (i = 0; i < plan[k].n; i++) {
				fresh();
				inject_prog = rnd_range(0, NW_PLAN_N - 1);
				inject_kind = plan[k].kind;
				rc = run_write();
				tally(hist, rc);
				if (rc != EX_FAIL)
					FAIL("%s: expected exit 3, got %d", plan[k].name, rc);
				if (n_prog_cycles != inject_prog + 1)
					cycles_after++;
				f0busy += M->f0_while_busy;
				if (plan[k].kind == 3)
					nm_wait(M, 5000);	/* the slow word completes on its own */
				converge(plan[k].name, &cv);
				total_runs += 3;
			}
			print_hist(plan[k].name, hist);
			printf("  %-34s program cycles after the fault %ld; F0 sent while busy %ld; converged: direct %ld, --resume %ld, --reset %ld, power cycle %ld, failed %ld\n",
			       "", cycles_after, f0busy, cv.direct, cv.resumed, cv.reset, cv.power_cycled, cv.failed);
		}
	}

	/* 5. preemption: wall clock jumps 20 ms while a word is programming */
	memset(hist, 0, sizeof(hist));
	for (i = 0; i < 50; i++) {
		fresh();
		jump_at = busy_ops[rnd_range(0, n_busy_ops - 1)];
		jump_us = 20000.0;
		rc = run_write();
		tally(hist, rc);
		if (rc != EX_OK || !chip_is(nw_sha_target))
			FAIL("preemption %ld: rc %d", i, rc);
		total_runs++;
	}
	print_hist("descheduled 20 ms mid-poll", hist);

	/* 6. wrong ID */
	{
		static const uint16_t ids[][3] = {
			{ 0x0001, 0x0001, 0x22ba }, { 0x007f, 0x0089, 0x22ba }, { 0x007f, 0x001c, 0x22b9 },
			{ 0x007f, 0x001c, 0x2249 }, { 0x00c2, 0x00c2, 0x22ba }, { 0x007f, 0x007f, 0x22ba },
		};
		unsigned k;

		memset(hist, 0, sizeof(hist));
		for (k = 0; k < sizeof(ids) / sizeof(ids[0]); k++) {
			fresh();
			M->id0 = ids[k][0];
			M->id100 = ids[k][1];
			M->id1 = ids[k][2];
			rc = run_write();
			tally(hist, rc);
			if (rc != EX_REFUSED || n_prog_cycles || !chip_is(nw_sha_stock) || M->state != NM_READ)
				FAIL("wrong id %u: rc %d, %ld program cycles", k, rc, n_prog_cycles);
			total_runs++;
		}
		print_hist("wrong manufacturer/device ID", hist);
	}

	/* 7. protected sector */
	memset(hist, 0, sizeof(hist));
	fresh();
	M->protected_mask |= 1u << 5;
	rc = run_write();
	tally(hist, rc);
	if (rc != EX_REFUSED || n_prog_cycles || !chip_is(nw_sha_stock))
		FAIL("SA5 protected: rc %d", rc);
	fresh();
	M->silent_protect_mask = 1u << 5;
	rc = run_write();
	tally(hist, rc);
	if (rc != EX_FAIL || n_prog_cycles != 1 || !chip_is(nw_sha_stock) || M->state != NM_READ)
		FAIL("SA5 silently protected: rc %d, %ld program cycles", rc, n_prog_cycles);
	memset(&cv, 0, sizeof(cv));
	converge("silent protect", &cv);
	total_runs += 5;
	print_hist("SA5 protected (reported, silent)", hist);

	/* 8. foreign images */
	{
		uint32_t bad_words[4] = { 0x4000, 0x14108, 0x1410b, 0x30000 };
		unsigned k;

		memset(hist, 0, sizeof(hist));
		for (k = 0; k < 4; k++) {
			fresh();
			if (k == 2) {	/* a plan word cleared beyond its target */
				uint32_t j = 0;

				while (nw_plan_target[j] == 0)
					j++;
				M->cells[nw_plan_word[j]] = 0;
			} else
				M->cells[bad_words[k]] ^= 0x0100;
			rc = run_write();
			tally(hist, rc);
			if (rc != EX_REFUSED || n_writes)
				FAIL("foreign image %u: rc %d, %ld writes", k, rc, n_writes);
			total_runs++;
		}
		print_hist("foreign image (1 word differs)", hist);
	}

	/* 9. bit-flip read errors */
	memset(hist, 0, sizeof(hist));
	memset(&cv, 0, sizeof(cv));
	for (i = 0; i < 600; i++) {
		fresh();
		flip_at = i < 300 ? rnd_range(1, total_ops) : rnd_range(first_prog_op - 20, last_prog_op + 20);
		flip_mask = (uint16_t)(1u << rnd_range(0, 15));
		rc = run_write();
		tally(hist, rc);
		converge("bit flip", &cv);
		total_runs += 3;
	}
	print_hist("single bit-flip on one read", hist);
	printf("  %-34s converged: direct %ld, --resume %ld, --reset %ld, power cycle %ld, failed %ld\n", "", cv.direct,
	       cv.resumed, cv.reset, cv.power_cycled, cv.failed);

	/* 10. partial state without --resume */
	fresh();
	cut_at = (first_prog_op + last_prog_op) / 2;
	cut_kind = 1;
	run_write();
	rc = run_write();
	if (rc != EX_PARTIAL || n_writes)
		FAIL("partial state without --resume: rc %d, %ld writes", rc, n_writes);
	rc = run("--check", NULL);
	if (rc != EX_PARTIAL)
		FAIL("--check on partial state: rc %d", rc);
	total_runs += 3;

	/* 11. SA0 not protected: refused with exit 6 before any program cycle, also from a partial state */
	fresh();
	M->protected_mask &= ~1u;
	rc = run_write();
	printf("SA0 not protected, stock chip: exit %d, %ld program cycles, %ld command writes, chip %s, %s\n", rc,
	       n_prog_cycles, n_writes, chip_is(nw_sha_stock) ? "stock" : "CHANGED",
	       M->state == NM_READ ? "read mode" : "NOT IN READ MODE");
	if (rc != EX_SA0 || n_prog_cycles || !chip_is(nw_sha_stock) || M->state != NM_READ)
		FAIL("SA0 not protected: rc %d, %ld program cycles", rc, n_prog_cycles);
	fresh();
	cut_at = (first_prog_op + last_prog_op) / 2;
	cut_kind = 2;
	run_write();
	{
		u8 before[32];

		chip_sha(before);
		M->protected_mask &= ~1u;
		rc = run_resume();
		printf("SA0 not protected, partial state, --resume: exit %d, %ld program cycles, chip %s\n", rc,
		       n_prog_cycles, chip_is(before) ? "unchanged" : "CHANGED");
		if (rc != EX_SA0 || n_prog_cycles || !chip_is(before) || M->state != NM_READ)
			FAIL("SA0 not protected, partial: rc %d", rc);
	}
	total_runs += 3;

	/* 12. single-instance lock and clock failure: refused before any bus access */
	{
		static const char *modes[4][2] = {
			{ "--write", CONFIRM }, { "--resume", CONFIRM }, { "--check", NULL }, { "--reset", NULL }
		};
		static const char *held[3] = { "4242", "", "42x" };	/* live copy; pid not yet written; garbage */
		long refused = 0, ops_seen = 0;
		unsigned k, h;

		fresh();
		lkf_other = 4242;
		for (h = 0; h < 3; h++)
			for (k = 0; k < 4; k++) {
				lock_put(held[h]);
				rc = run(modes[k][0], modes[k][1]);
				refused += rc == EX_LOCKED;
				ops_seen += n_ops;
				if (rc != EX_LOCKED || n_ops || !lkf_exists || lkf_len != (long)strlen(held[h]) ||
				    memcmp(lkf_buf, held[h], lkf_len))
					FAIL("lock held (%s) %s: rc %d, %ld bus ops", held[h], modes[k][0], rc, n_ops);
				total_runs++;
			}
		lock_put("4242");
		rc = run("--dry-run", NULL);
		if (rc != EX_OK || n_ops)
			FAIL("--dry-run with the lock held: rc %d", rc);
		printf("lock held by a live copy, empty or garbage: %ld of 12 runs exit 7, %ld bus ops; --dry-run still exit %d\n",
		       refused, ops_seen, rc);

		lkf_other = 0;	/* the holder died */
		lock_put("4242");
		rc = run_write();
		printf("stale lock of a dead pid: exit %d, %s, lock %s\n", rc, chip_is(nw_sha_target) ? "target" : "NOT TARGET",
		       lkf_exists ? "LEFT" : "removed");
		if (rc != EX_OK || !chip_is(nw_sha_target) || lkf_exists || !memmem(outbuf, outlen, "removing stale", 14))
			FAIL("stale lock: rc %d", rc);
		fresh();
		snprintf(path, sizeof(path), "%u", pid_now + 1);	/* our own pid, reused after a reboot */
		lock_put(path);
		rc = run_write();
		if (rc != EX_OK || lkf_exists)
			FAIL("lock with our own pid: rc %d", rc);
		lkf_nodir = 1;
		rc = run_write();
		printf("lock with our own pid: exit 0; lock cannot be created: exit %d, %ld bus ops\n", rc, n_ops);
		if (rc != EX_LOCKED || n_ops)
			FAIL("lock not creatable: rc %d, %ld ops", rc, n_ops);
		lkf_nodir = 0;
		fresh();
		clock_bad = 1;
		rc = run_write();
		clock_bad = 0;
		printf("clock_gettime failure: exit %d, %ld bus ops, lock %s\n", rc, n_ops, lkf_exists ? "LEFT" : "removed");
		if (rc != EX_REFUSED || n_ops || lkf_exists)
			FAIL("clock failure: rc %d, %ld ops", rc, n_ops);
		total_runs += 5;
	}
	printf("stale locks left by cut or killed runs and removed by the next run: %ld\n", n_stale_removed);

	printf("totals: %ld writer runs, %ld bus writes checked by the monitor, %ld monitor failures, %ld erase-capable cycles, %ld distinct intermediate states (%ld snapshots)\n",
	       total_runs, mon_total_writes, mon_failures, mon_erase_capable, n_snap, n_snap_states);
	return 0;
}

/* ---- fuzz transcript for the C/Python model comparison ------------------------------------------------ */
static void fz_w(uint32_t w, uint16_t v)
{
	nm_write(M, w, v);
	fprintf(tr, "W %x %x\n", w, v);
}

static void fz_r(uint32_t w)
{
	fprintf(tr, "R %x %x\n", w, nm_read(M, w));
}

static void fz_t(double us)
{
	nm_wait(M, us);
	fprintf(tr, "T %.17g\n", us);
}

static int suite_fuzz(const char *path, long n)
{
	long i;
	int j;

	tr = fopen(path, "w");
	fprintf(tr, "# fuzz erase_us=3000\n");
	fresh();
	M->erase_us = 3000.0;
	for (i = 0; i < n; i++) {
		uint32_t w = (uint32_t)rnd_range(0, NM_WORDS - 1);
		uint32_t near = 0x14100 + (uint32_t)rnd_range(0, 0x7ff);

		switch (rnd_range(0, 11)) {
		case 0: case 1: fz_r(rnd_range(0, 1) ? w : near); break;
		case 2:	/* program, possibly 0->1, possibly protected SA0 */
			fz_w(0x555, 0xaa); fz_w(0x2aa, 0x55); fz_w(0x555, 0xa0);
			fz_w(rnd_range(0, 3) ? near : (uint32_t)rnd_range(0, 0x1fff), (uint16_t)rnd());
			for (j = 0; j < 6; j++)
				fz_r(near);
			break;
		case 3:	/* autoselect */
			fz_w(0x555, 0xaa); fz_w(0x2aa, 0x55); fz_w(0x555, 0x90);
			fz_r(0); fz_r(0x100); fz_r(1); fz_r(nm_sector_base[rnd_range(0, 10)] + 2); fz_r(0x200);
			if (rnd_range(0, 3))
				fz_w(0, 0xf0);
			break;
		case 4:	/* sector erase, any sector */
			fz_w(0x555, 0xaa); fz_w(0x2aa, 0x55); fz_w(0x555, 0x80);
			fz_w(0x555, 0xaa); fz_w(0x2aa, 0x55);
			fz_w(nm_sector_base[rnd_range(0, 10)] + (uint32_t)rnd_range(0, 0xfff), 0x30);
			fz_r(w); fz_r(w); fz_w(0, 0xf0); fz_r(w);
			break;
		case 5: fz_w(0, 0xf0); break;
		case 6: fz_t((double)rnd_range(1, 4000)); break;
		case 7: fz_w(w, (uint16_t)rnd()); break;	/* garbage write */
		case 8: fz_w(0x55, 0x98); fz_r(0x10); break;
		case 9: fz_w(0x555 | (uint32_t)rnd_range(0, 1) << 11, 0xaa); fz_w(rnd_range(0, 1) ? 0x2aa : 0x2ab, 0x55); break;
		case 10: if (rnd_range(0, 40) == 0) { fz_w(0x555, 0xaa); fz_w(0x2aa, 0x55); fz_w(0x555, 0x80);
				fz_w(0x555, 0xaa); fz_w(0x2aa, 0x55); fz_w(0x555, 0x10); fz_t(40000); } break;
		default: fz_w(0x555, 0xaa); fz_w(0x2aa, 0x55); fz_w(0x555, (uint16_t)rnd_range(0, 255)); fz_r(near); break;
		}
	}
	for (i = 0; i < (long)NM_WORDS; i += 97)
		fz_r((uint32_t)i);
	fclose(tr);
	tr = NULL;
	return 0;
}

#ifdef NORWRITE_ERASE_EXPERIMENT
/* ---- R2/R3 analysis (host only) ----------------------------------------------------------------------- */
static uint32_t r3_ops_word[0x40000];
static uint16_t r3_ops_val[0x40000];
static long r3_n, r3_sa5_step, r3_tail_end_step, win_lo, win_hi;

static int r3_sequence(void)	/* returns 0 on success, -1 cut, >0 step failed */
{
	long i;

	if (setjmp(cut_jb)) {
		cut_at = -1;
		return -1;
	}
	for (i = 0; i < r3_n; i++) {
		int r;

		if (i == r3_sa5_step)
			win_lo = n_ops;
		if (i == r3_tail_end_step)
			win_hi = n_ops;
		r = r3_ops_word[i] >= 0x80000000u ? norwrite_exp_erase(r3_ops_word[i] & 0xff)
						       : norwrite_exp_program(r3_ops_word[i], r3_ops_val[i]);
		if (r)
			return 1;
	}
	return 0;
}

static void r3_build(void)
{
	static const int order[2] = { 6, 5 };
	int k;
	uint32_t w;

	r3_n = 0;
	for (k = 0; k < 2; k++) {
		int s = order[k];
		uint32_t base = nm_sector_base[s], end = base + nm_sector_len[s];
		int pass;

		if (s == 5)
			r3_sa5_step = r3_n;
		r3_ops_word[r3_n++] = 0x80000000u | (uint32_t)s;
		for (pass = 0; pass < 2; pass++) {	/* SA5: gzip tail (bytes below 0x247DE) first */
			if (s == 5 && pass == 1)
				r3_tail_end_step = r3_n;
			for (w = base; w < end; w++) {
				uint16_t v = homtom[2 * w] | homtom[2 * w + 1] << 8;
				int tail = w < 0x247de / 2;

				if (v == 0xffff || (pass == 0) != (s != 5 || tail))
					continue;
				r3_ops_word[r3_n] = w;
				r3_ops_val[r3_n++] = v;
			}
		}
	}
}

static int suite_erase(const char *outdir)
{
	char path[1024];
	FILE *f;
	int s, rc;
	long i, total, brick = 0, ok_boot = 0, n = 0, completed = 0, brick_outside = 0, in_window = 0;
	uint8_t img[IMG_BYTES];
	static const double frac[3] = { 0.1, 0.5, 0.9 };

	monitor_on = 0;
	/* (a) each sector: interrupted erase at 10/50/90 % of the typical time; images for the classifier */
	snprintf(path, sizeof(path), "%s/erase-cut.bin", outdir);
	f = fopen(path, "wb");
	for (s = 0; s < NM_NSECT; s++) {
		int k;

		for (k = 0; k < 3; k++) {
			fresh();
			M->rng ^= (uint64_t)(s * 3 + k + 1) * 0x9e3779b97f4a7c15ull;
			if (setjmp(cut_jb) == 0) {
				n_ops = 0;
				cut_at = 6 + (long)(frac[k] * M->erase_us / M->access_us / 1.0);
				cut_kind = 1;
				norwrite_exp_erase((u32)s);
			}
			cut_at = -1;
			nm_image(M, img);
			fwrite(img, 1, IMG_BYTES, f);
		}
		fresh();
		n_ops = 0;
		rc = norwrite_exp_erase((u32)s);
		nm_image(M, img);
		fwrite(img, 1, IMG_BYTES, f);	/* fourth image: the uninterrupted result */
		printf("erase SA%-2d uninterrupted: rc %d (0 ok, -3 data not erased = protected)\n", s, rc);
	}
	fclose(f);

	/* (b) R3 sequence with cuts */
	r3_build();
	fresh();
	n_ops = 0;
	M->erase_us = 50000.0;	/* scaled-down erase so the sweep stays fast; positions scale with it */
	rc = r3_sequence();
	total = n_ops;
	printf("R3 brick window (SA5 erase start to end of gzip-tail program): ops %ld-%ld of %ld (%.1f%%)\n", win_lo,
	       win_hi, total, 100.0 * (win_hi - win_lo) / total);
	{
		u8 d[32];
		uint8_t want[32];
		struct sha hs;
		uint32_t b;

		chip_sha(d);
		sha_init(&hs);
		for (b = 0; b < IMG_BYTES; b++)
			sha_byte(&hs, homtom[b]);
		sha_final(&hs, want);
		printf("R3 full sequence: rc %d, %ld steps, %ld bus ops, result %s mtd0-homtom.bin\n", rc, r3_n, total,
		       eq32(d, want) ? "equals" : "DIFFERS FROM");
		if (rc || !eq32(d, want))
			FAIL("R3 sequence");
	}
	for (i = 0; i < 1000; i++) {
		long at = rnd_range(1, total);
		long lo = win_lo, hi = win_hi;

		fresh();
		M->erase_us = 50000.0;
		M->rng ^= (uint64_t)at * 0x9e3779b97f4a7c15ull;
		n_ops = 0;
		cut_at = at;
		cut_kind = 1;
		rc = r3_sequence();
		if (rc == 0)
			completed++;
		nm_image(M, img);
		n++;
		if (memcmp(img, stock, 0x247dd) == 0 || memcmp(img, homtom, 0x247dd) == 0)
			ok_boot++;
		else {
			brick++;
			if (at < lo || at > hi)
				brick_outside++;
		}
		if (at >= lo && at <= hi)
			in_window++;
		win_lo = lo;
		win_hi = hi;
	}
	printf("R3 cuts inside the window %ld; boot damage outside the window %ld\n", in_window, brick_outside);
	printf("R3 cuts: %ld uniform over bus ops; boot chain intact (bytes 0-0x247DC stock or homtom) %ld, damaged %ld, completed %ld\n",
	       n, ok_boot, brick, completed);
	return 0;
}
#endif

int main(int argc, char **argv)
{
	FILE *f;
	uint32_t w, i;

	if (argc < 3) {
		fprintf(stderr, "usage: see harness.c\n");
		return 2;
	}
	f = fopen(argv[2], "rb");
	if (!f || fread(stock, 1, IMG_BYTES, f) != IMG_BYTES) {
		fprintf(stderr, "cannot read %s\n", argv[2]);
		return 2;
	}
	fclose(f);
	for (w = 0; w < NM_WORDS; w++)
		plan_idx[w] = -1;
	for (i = 0; i < NW_PLAN_N; i++)
		plan_idx[nw_plan_word[i]] = (int32_t)i;
	nw_host_lock = host_lock;
	if (!strcmp(argv[1], "r1") && argc >= 4) {
		char path[1024];

		if (argc >= 5)
			rs ^= strtoull(argv[4], NULL, 0) * 0x9e3779b97f4a7c15ull;
		snprintf(path, sizeof(path), "%s/snapshots.bin", argv[3]);
		snap = fopen(path, "wb");
		suite_r1(argv[3]);
		fclose(snap);
	} else if (!strcmp(argv[1], "fuzz") && argc >= 6) {
		rs ^= strtoull(argv[4], NULL, 0) * 0x9e3779b97f4a7c15ull;
		suite_fuzz(argv[3], strtol(argv[5], NULL, 0));
	}
#ifdef NORWRITE_ERASE_EXPERIMENT
	else if (!strcmp(argv[1], "erase") && argc >= 5) {
		f = fopen(argv[3], "rb");
		if (!f || fread(homtom, 1, IMG_BYTES, f) != IMG_BYTES)
			return 2;
		fclose(f);
		suite_erase(argv[4]);
	}
#endif
	else {
		fprintf(stderr, "bad arguments\n");
		return 2;
	}
	printf("harness failures: %ld\n", failures);
	return failures ? 1 : 0;
}
