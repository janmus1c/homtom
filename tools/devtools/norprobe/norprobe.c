/* norprobe: probe the GO 630 NOR (EN29LV400BB, 512 KiB, x16, physical 0) from Linux 6.1 via /dev/mem.
 * DRAFT for docs/NOR_PLAN.md stages N1 and N2. Not built by tools/devtools/build.sh (subdirectory) and not
 * part of any image. Build by hand after review, with the build.sh compiler flags.
 *
 *   norprobe dump FILE              N1: copy the array to FILE and print FNV-1a hashes. No bus writes.
 *   norprobe id SEND-ID-COMMANDS    N2: autoselect and CFI query, reset, re-verify the array.
 *
 * The only bus writes in this file go through cmd(), which accepts exactly the pairs in allowed[]:
 * unlock, autoselect (90), CFI query (98) and reset (F0). There is no program (A0) or erase (80/30/10)
 * sequence, so the worst outcome of a fault is a chip left in ID/CFI mode, which a reset command or a
 * power cycle clears. Exit 3 means the array did not read back after reset: do not reboot, retry reset.
 * Freestanding (raw EABI syscalls, no libc): the target is ARMv4T and Debian's glibc is ARMv5TE. */
typedef unsigned int u32;
typedef unsigned short u16;

#define NOR_SIZE 0x80000u
#define LOW_SIZE 0x40000u
#define STOCK_LOW_FNV 0x17f85537u	/* bytes 0x00000-0x3FFFF of artifacts/backups/mtd0-before-*.bin (SHA-256 d48b41...) */
#define STOCK_ALL_FNV 0x3642476au
#define HOMTOM_ALL_FNV 0x0bf85f0eu	/* artifacts/homtom-nor/mtd0-homtom.bin */
#define NSECT 11
#define NCHECK (NSECT * 4 + 6)

static const u32 sector_byte[NSECT] = {
	0x00000, 0x04000, 0x06000, 0x08000, 0x10000, 0x20000, 0x30000, 0x40000, 0x50000, 0x60000, 0x70000
};

static const u32 allowed[][2] = {
	{ 0x555, 0xaa }, { 0x2aa, 0x55 }, { 0x555, 0x90 }, { 0x055, 0x98 }, { 0x000, 0xf0 }
};

static volatile u16 *nor;
static u32 check_word[NCHECK];
static u16 check_val[NCHECK];
static u16 buf[2048];

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

static void put(const char *s)
{
	long n = 0;

	while (s[n])
		n++;
	sc(4, 1, (long)s, n, 0, 0, 0);
}

static void hex(u32 v, int digits)
{
	char b[8];
	int i;

	for (i = 0; i < digits; i++)
		b[i] = "0123456789abcdef"[(v >> (4 * (digits - 1 - i))) & 15];
	sc(4, 1, (long)b, digits, 0, 0, 0);
}

static void kv(const char *k, u32 v, int digits)
{
	put(k);
	hex(v, digits);
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

static u32 fnv(u32 bytes)
{
	u32 h = 0x811c9dc5u, i;

	for (i = 0; i < bytes / 2; i++) {
		u16 w = nor[i];

		h = (h ^ (w & 0xff)) * 0x01000193u;
		h = (h ^ (w >> 8)) * 0x01000193u;
	}
	return h;
}

static int map(int writable)
{
	long fd = sc(5, (long)"/dev/mem", (writable ? 2 : 0) | 0x101000, 0, 0, 0, 0);	/* O_SYNC: uncached */
	long m;

	if (bad(fd))
		return -1;
	m = sc(192, 0, NOR_SIZE, writable ? 3 : 1, 1, fd, 0);	/* mmap2 MAP_SHARED, page 0 = nGCS0 */
	if (bad(m))
		return -1;
	nor = (volatile u16 *)m;
	return 0;
}

static int cmd(u32 word, u32 val)
{
	u32 i;

	for (i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++)
		if (allowed[i][0] == word && allowed[i][1] == val) {
			nor[word] = (u16)val;
			__asm__ volatile("" ::: "memory");
			return 0;
		}
	return -1;
}

static void snapshot(void)
{
	static const u32 extra[6] = { 0x555, 0x2aa, 0x055, 0x010, 0x011, 0x012 };
	u32 i, n = 0;

	for (i = 0; i < NSECT; i++) {
		u32 w = sector_byte[i] / 2;

		check_word[n++] = w;
		check_word[n++] = w + 1;
		check_word[n++] = w + 2;
		check_word[n++] = w + 3;
	}
	for (i = 0; i < 6; i++)
		check_word[n++] = extra[i];
	for (i = 0; i < NCHECK; i++)
		check_val[i] = nor[check_word[i]];
}

static int array_ok(void)
{
	u32 i;

	for (i = 0; i < NCHECK; i++)
		if (nor[check_word[i]] != check_val[i])
			return 0;
	return 1;
}

static int reset_and_verify(void)
{
	int tries;

	for (tries = 0; tries < 3; tries++) {
		cmd(0, 0xf0);
		if (array_ok())
			return 1;
	}
	return 0;
}

static int dump(const char *path)
{
	long fd;
	u32 off, i;

	if (map(0))
		return 1;
	fd = sc(5, (long)path, 0x241, 0644, 0, 0, 0);	/* O_WRONLY|O_CREAT|O_TRUNC */
	if (bad(fd))
		return 1;
	for (off = 0; off < NOR_SIZE / 2; off += 2048) {
		for (i = 0; i < 2048; i++)
			buf[i] = nor[off + i];
		if (sc(4, fd, (long)buf, sizeof(buf), 0, 0, 0) != (long)sizeof(buf))
			return 1;
	}
	sc(118, fd, 0, 0, 0, 0, 0);
	sc(6, fd, 0, 0, 0, 0, 0);
	{
		u32 low = fnv(LOW_SIZE), all = fnv(NOR_SIZE);

		kv("fnv_low ", low, 8);
		kv("fnv_all ", all, 8);
		put(low == STOCK_LOW_FNV ? "boot+gfx region matches stock backup\n" : "boot+gfx region DIFFERS from stock backup\n");
		put(all == STOCK_ALL_FNV ? "whole chip matches stock backup\n" :
		    all == HOMTOM_ALL_FNV ? "whole chip matches homtom image\n" : "whole chip differs from both references\n");
	}
	return 0;
}

static int ident(void)
{
	u32 sigset[2] = { 0xffffffffu, 0xffffffffu }, old[2];
	u16 mfr, dev, prot[NSECT], cfi[0x2d];
	int ok1, ok2, has_cfi = 0;
	u32 i;

	if (map(1))
		return 1;
	if (fnv(LOW_SIZE) != STOCK_LOW_FNV) {
		put("array does not match the stock backup; refusing to send commands\n");
		return 4;
	}
	snapshot();
	sc(175, 0, (long)sigset, (long)old, 8, 0, 0);	/* rt_sigprocmask(SIG_BLOCK) */

	cmd(0x555, 0xaa);
	cmd(0x2aa, 0x55);
	cmd(0x555, 0x90);
	mfr = nor[0];
	dev = nor[1];
	for (i = 0; i < NSECT; i++)
		prot[i] = nor[sector_byte[i] / 2 + 2];
	ok1 = reset_and_verify();

	if (ok1) {
		cmd(0x055, 0x98);
		for (i = 0; i < 0x2d; i++)
			cfi[i] = nor[0x10 + i];
		has_cfi = (cfi[0] & 0xff) == 'Q' && (cfi[1] & 0xff) == 'R' && (cfi[2] & 0xff) == 'Y';
		ok2 = reset_and_verify();
	} else {
		ok2 = 0;
	}

	sc(175, 2, (long)old, 0, 8, 0, 0);	/* SIG_SETMASK */

	kv("mfr ", mfr, 4);
	kv("dev ", dev, 4);
	for (i = 0; i < NSECT; i++) {
		put("protect ");
		hex(sector_byte[i], 5);
		kv(" ", prot[i], 4);
	}
	if (ok1) {
		put(has_cfi ? "cfi QRY present\n" : "cfi not present\n");
		if (has_cfi)
			for (i = 0; i < 0x2d; i++) {
				put("cfi ");
				hex(0x10 + i, 2);
				kv(" ", cfi[i], 4);
			}
	}
	if (!ok1 || !ok2) {
		put("ARRAY DID NOT RETURN TO READ MODE: do not reboot; rerun `norprobe id` only after review\n");
		return 3;
	}
	put("array verified after reset\n");
	return 0;
}

int main_c(long *sp)
{
	int argc = (int)sp[0];
	char **argv = (char **)(sp + 1);

	if (argc == 3 && same(argv[1], "dump"))
		return dump(argv[2]);
	if (argc == 3 && same(argv[1], "id") && same(argv[2], "SEND-ID-COMMANDS"))
		return ident();
	put("usage: norprobe dump FILE | norprobe id SEND-ID-COMMANDS\n");
	return 2;
}

__asm__(".globl _start\n_start:\n mov r0, sp\n bl main_c\n mov r7, #1\n svc 0\n");
