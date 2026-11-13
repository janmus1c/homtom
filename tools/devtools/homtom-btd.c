/* homtom-btd: BlueZ-less Bluetooth Classic serial endpoint for the homtom status JSON.
 * Freestanding ARMv4T, raw EABI syscalls, no libc.
 *
 *   homtom-btd [-o] [-l] [-d N]
 *     -o   do not force link-level authentication (Write_Authentication_Enable 0); debug only
 *     -l   RFCOMM security LOW instead of MEDIUM (no encryption request from our side)
 *     -d N use hciN (default 0)
 *
 * Division of labour (Linux 6.1, no mgmt socket ever opened, so HCI_MGMT stays clear):
 *   kernel  accepts ACL connections (an L2CAP listener exists), runs L2CAP, RFCOMM, encryption setup.
 *   us      PIN Code Request, Link Key Request, Link Key Notification on a raw HCI socket (the kernel
 *           ignores all three without HCI_MGMT; HCIDEVUP sets HCI_BONDABLE so it sends no PIN negative reply),
 *           a one-record SDP server on L2CAP PSM 1, and the RFCOMM channel 1 server.
 *
 * Files:
 *   /mnt/internal/homtom/bt.pin    PIN, 1..16 characters; absent = every pairing is refused
 *   /tmp/bt.pair                   pairing window; absent = new pairings refused; removed after a key is stored or when
 *                                  the window ends: the file may hold its length in seconds (30..1800), default 300
 *   /mnt/internal/homtom/btkeys    link keys, 24-byte records: addr[6] type pinlen key[16]
 *
 * RFCOMM line protocol: on connect one JSON line; then "status", "ping", "help", "quit". */

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#define PINF "/mnt/internal/homtom/bt.pin"
#define KEYF "/mnt/internal/homtom/btkeys"
#define KEYT "/mnt/internal/homtom/btkeys.tmp"
#define PAIRF "/tmp/bt.pair"
#define STATUS "/bin/homtom-status"
#define CHANNEL 1
#define MAXKEYS 8
#define NSDP 4
#define NRC 3
#define PAIR_DEFAULT 300	/* pairing window length when /tmp/bt.pair holds no number */
#define PAIR_MIN 30
#define PAIR_MAX 1800

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

#define SYS_exit 1
#define SYS_fork 2
#define SYS_read 3
#define SYS_write 4
#define SYS_open 5
#define SYS_close 6
#define SYS_unlink 10
#define SYS_execve 11
#define SYS_kill 37
#define SYS_rename 38
#define SYS_pipe 42
#define SYS_ioctl 54
#define SYS_dup2 63
#define SYS_wait4 114
#define SYS_fsync 118
#define SYS_poll 168
#define SYS_socket 281
#define SYS_bind 282
#define SYS_listen 284
#define SYS_accept 285
#define SYS_sendto 290
#define SYS_recvfrom 292
#define SYS_setsockopt 294
#define SYS_clock_gettime 263

#define AF_BT 31
#define POLLIN 1
#define POLLERR 8
#define POLLHUP 16
#define MSG_NOSIGNAL 0x4000

struct pfd { int fd; short ev, rev; };

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void cpy(u8 *d, const u8 *s, int n) { while (n-- > 0) *d++ = *s++; }
static void zero(u8 *d, int n) { while (n-- > 0) *d++ = 0; }
static int same(const u8 *a, const u8 *b, int n) { while (n-- > 0) if (*a++ != *b++) return 0; return 1; }

/* ---- logging to stderr ---- */
static char lb[256];
static int ll;
static void lc(char c) { if (ll < 254) lb[ll++] = c; }
static void ls(const char *s) { while (*s) lc(*s++); }
static void lx(u32 v, int digits) { while (digits--) lc("0123456789abcdef"[(v >> (digits * 4)) & 15]); }
static void ld(long v)
{
	static const long p[] = { 1000000000, 100000000, 10000000, 1000000, 100000, 10000, 1000, 100, 10, 1 };
	int i, started = 0;

	if (v < 0) { lc('-'); v = -v; }
	for (i = 0; i < 10; i++) {
		int d = 0;

		while (v >= p[i]) { v -= p[i]; d++; }
		if (d || started || i == 9) { lc('0' + d); started = 1; }
	}
}
static void la(const u8 *a)	/* bdaddr, HCI byte order is little-endian */
{
	int i;

	for (i = 5; i >= 0; i--) { lx(a[i], 2); if (i) lc(':'); }
}
static void lend(void) { lc('\n'); sc(SYS_write, 2, (long)lb, ll, 0, 0, 0); ll = 0; }
static void logs(const char *s) { ls("btd: "); ls(s); lend(); }

/* ---- small file helpers ---- */
static int readfile(const char *path, u8 *buf, int cap)
{
	long fd = sc(SYS_open, (long)path, 0, 0, 0, 0, 0), n, t = 0;

	if (fd < 0)
		return -1;
	while (t < cap && (n = sc(SYS_read, fd, (long)(buf + t), cap - t, 0, 0, 0)) > 0)
		t += n;
	sc(SYS_close, fd, 0, 0, 0, 0, 0);
	return (int)t;
}
static int exists(const char *path)
{
	long fd = sc(SYS_open, (long)path, 0, 0, 0, 0, 0);

	if (fd < 0)
		return 0;
	sc(SYS_close, fd, 0, 0, 0, 0, 0);
	return 1;
}

/* ---- HCI ---- */
static long hci;
static int opt_open, opt_low, dev;

static void hci_cmd(u16 op, const u8 *p, int n)
{
	u8 pkt[4 + 32];
	long r;

	pkt[0] = 1;
	pkt[1] = op & 0xff;
	pkt[2] = op >> 8;
	pkt[3] = n;
	cpy(pkt + 4, p, n);
	r = sc(SYS_sendto, hci, (long)pkt, 4 + n, 0, 0, 0);
	if (r < 0) { ls("btd: hci send "); lx(op, 4); ls(" err "); ld(r); lend(); }
}

/* key store: 24-byte records addr[6] type pinlen key[16] */
static u8 keys[MAXKEYS * 24];

static int keys_load(void)
{
	int n = readfile(KEYF, keys, sizeof keys);
	int k = 0;

	if (n > 0)
		while (n >= 24) { n -= 24; k++; }
	return k;
}

static void keys_save(int k)
{
	long fd = sc(SYS_open, (long)KEYT, 1 | 0x40 | 0x200, 0600, 0, 0, 0);	/* O_WRONLY|O_CREAT|O_TRUNC */

	if (fd < 0) { ls("btd: key store open err "); ld(fd); lend(); return; }
	sc(SYS_write, fd, (long)keys, k * 24, 0, 0, 0);
	sc(SYS_fsync, fd, 0, 0, 0, 0, 0);
	sc(SYS_close, fd, 0, 0, 0, 0, 0);
	if (sc(SYS_rename, (long)KEYT, (long)KEYF, 0, 0, 0, 0) < 0)
		logs("key store rename failed");
}

static void on_link_key_req(const u8 *a)
{
	int k = keys_load(), i;

	for (i = 0; i < k; i++)
		if (same(keys + i * 24, a, 6)) {
			u8 p[22];

			cpy(p, a, 6);
			cpy(p + 6, keys + i * 24 + 8, 16);
			hci_cmd(0x040b, p, 22);		/* Link_Key_Request_Reply */
			ls("btd: link key request "); la(a); ls(" -> stored key"); lend();
			return;
		}
	hci_cmd(0x040c, a, 6);				/* Link_Key_Request_Negative_Reply: remote falls back to PIN */
	ls("btd: link key request "); la(a); ls(" -> none"); lend();
}

/* ---- pairing window: opened by creating /tmp/bt.pair, closed here when its time is up ---- */
static int pair_open;
static u32 pair_t0, pair_len;

static u32 mono_s(void)
{
	long ts[2];

	if (sc(SYS_clock_gettime, 1, (long)ts, 0, 0, 0, 0) < 0)	/* CLOCK_MONOTONIC: the wall clock is set late by the bridge */
		return 0;
	return (u32)ts[0];
}

static void pair_check(void)
{
	u8 b[12];
	u32 now = mono_s(), v = 0;
	int n, i;

	if (!exists(PAIRF)) {
		pair_open = 0;
		return;
	}
	if (!pair_open) {
		n = readfile(PAIRF, b, sizeof b);
		for (i = 0; i < n && b[i] >= '0' && b[i] <= '9' && v < 100000; i++)
			v = v * 10 + (b[i] - '0');
		pair_len = !v ? PAIR_DEFAULT : v < PAIR_MIN ? PAIR_MIN : v > PAIR_MAX ? PAIR_MAX : v;
		pair_t0 = now;
		pair_open = 1;
		ls("btd: pairing window open for "); ld(pair_len); ls(" s"); lend();
		return;
	}
	if (now - pair_t0 >= pair_len) {
		sc(SYS_unlink, (long)PAIRF, 0, 0, 0, 0, 0);
		pair_open = 0;
		logs("pairing window closed, time is up");
	}
}

static u8 last_pinlen;

static void on_pin_req(const u8 *a)
{
	u8 pin[20], p[23];
	int n;

	pair_check();
	if (!exists(PAIRF)) {
		hci_cmd(0x040e, a, 6);			/* PIN_Code_Request_Negative_Reply */
		ls("btd: pin request "); la(a); ls(" refused, no " PAIRF); lend();
		return;
	}
	n = readfile(PINF, pin, 16);
	while (n > 0 && (pin[n - 1] == '\n' || pin[n - 1] == '\r' || pin[n - 1] == ' '))
		n--;
	if (n <= 0) {
		hci_cmd(0x040e, a, 6);
		ls("btd: pin request "); la(a); ls(" refused, no " PINF); lend();
		return;
	}
	cpy(p, a, 6);
	p[6] = n;
	zero(p + 7, 16);
	cpy(p + 7, pin, n);
	hci_cmd(0x040d, p, 23);				/* PIN_Code_Request_Reply */
	last_pinlen = n;
	ls("btd: pin request "); la(a); ls(" -> replied, len "); ld(n); lend();
}

static void on_link_key_notify(const u8 *e)	/* addr[6] key[16] type */
{
	int k = keys_load(), i;
	u8 *r = 0;

	for (i = 0; i < k; i++)
		if (same(keys + i * 24, e, 6))
			r = keys + i * 24;
	if (!r) {
		if (k == MAXKEYS) {
			for (i = 0; i < (MAXKEYS - 1) * 24; i++)
				keys[i] = keys[i + 24];
			k--;
		}
		r = keys + k * 24;
		k++;
	}
	cpy(r, e, 6);
	r[6] = e[22];
	r[7] = last_pinlen;
	cpy(r + 8, e + 6, 16);
	keys_save(k);
	sc(SYS_unlink, (long)PAIRF, 0, 0, 0, 0, 0);
	pair_open = 0;
	ls("btd: link key stored for "); la(e); ls(" type "); ld(e[22]); ls(", pairing window closed"); lend();
}

/* returns -1 on a socket error (device gone: exit and let the supervisor restart us) */
/* The chip leaves page/inquiry scan after a link comes or goes and the kernel does not restore it (no mgmt): Write_Scan_Enable = 3 again. */
static void scan_on(void)
{
	u8 v = 3;

	hci_cmd(0x0c1a, &v, 1);
}

static int hci_event(void)
{
	u8 b[260];
	long n = sc(SYS_recvfrom, hci, (long)b, sizeof b, 0, 0, 0);
	const u8 *e = b + 3;

	if (n <= 0) { ls("btd: hci recv "); ld(n); lend(); return -1; }
	if (n < 3 || b[0] != 4 || n < 3 + b[2])
		return 0;
	switch (b[1]) {
	case 0x03: ls("btd: conn complete status "); lx(e[0], 2); ls(" "); la(e + 3); ls(" encr "); ld(e[10]); lend();
		scan_on();
		break;
	case 0x04: ls("btd: conn request "); la(e); ls(" class "); lx(e[8] << 16 | e[7] << 8 | e[6], 6); lend(); break;
	case 0x05: ls("btd: disconnect status "); lx(e[0], 2); ls(" reason "); lx(e[3], 2); lend();
		scan_on();
		break;
	case 0x06: ls("btd: auth complete status "); lx(e[0], 2); lend(); break;
	case 0x08: ls("btd: encrypt change status "); lx(e[0], 2); ls(" on "); ld(e[3]); lend(); break;
	case 0x0e:
		if (b[2] >= 4 && (e[2] == 0x04 || (e[2] == 0x0c && e[1] == 0x20))) {
			ls("btd: cmd complete "); lx(e[2] << 8 | e[1], 4); ls(" status "); lx(e[3], 2); lend();
		}
		break;
	case 0x0f:
		if (e[0] && e[3] == 0x04) { ls("btd: cmd status "); lx(e[3] << 8 | e[2], 4); ls(" err "); lx(e[0], 2); lend(); }
		break;
	case 0x16: on_pin_req(e); break;
	case 0x17: on_link_key_req(e); break;
	case 0x18: if (b[2] >= 23) on_link_key_notify(e); break;
	}
	return 0;
}

/* ---- SDP server, one Serial Port record ---- */
#define HANDLE 0x00010000

static const u8 v0000[] = { 0x0a, 0x00, 0x01, 0x00, 0x00 };
static const u8 v0001[] = { 0x35, 0x03, 0x19, 0x11, 0x01 };
static const u8 v0004[] = { 0x35, 0x0c, 0x35, 0x03, 0x19, 0x01, 0x00, 0x35, 0x05, 0x19, 0x00, 0x03, 0x08, CHANNEL };
static const u8 v0005[] = { 0x35, 0x03, 0x19, 0x10, 0x02 };
static const u8 v0006[] = { 0x35, 0x09, 0x09, 0x65, 0x6e, 0x09, 0x00, 0x6a, 0x09, 0x01, 0x00 };
static const u8 v0009[] = { 0x35, 0x08, 0x35, 0x06, 0x19, 0x11, 0x01, 0x09, 0x01, 0x02 };
static const u8 v0100[] = { 0x25, 0x0d, 'h', 'o', 'm', 't', 'o', 'm', ' ', 's', 't', 'a', 't', 'u', 's' };

static const struct { u16 id; u8 len; const u8 *v; } rec[] = {
	{ 0x0000, sizeof v0000, v0000 }, { 0x0001, sizeof v0001, v0001 }, { 0x0004, sizeof v0004, v0004 },
	{ 0x0005, sizeof v0005, v0005 }, { 0x0006, sizeof v0006, v0006 }, { 0x0009, sizeof v0009, v0009 },
	{ 0x0100, sizeof v0100, v0100 },
};
#define NREC (int)(sizeof rec / sizeof rec[0])
static const u32 rec_uuid[] = { 0x1101, 0x0100, 0x0003, 0x1002 };
static const u8 base_uuid[12] = { 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb };

/* data element header: returns header size and payload length, 0 on malformed input */
static int de(const u8 *p, const u8 *e, int *len)
{
	int s;

	if (p >= e)
		return 0;
	s = p[0] & 7;
	if ((p[0] >> 3) == 0) { *len = 0; return 1; }
	if (s < 5) { *len = 1 << s; return p + 1 + *len <= e ? 1 : 0; }
	if (s == 5) { if (p + 2 > e) return 0; *len = p[1]; return p + 2 + *len <= e ? 2 : 0; }
	if (s == 6) { if (p + 3 > e) return 0; *len = p[1] << 8 | p[2]; return p + 3 + *len <= e ? 3 : 0; }
	if (p + 5 > e) return 0;
	*len = p[3] << 8 | p[4];
	return p[1] || p[2] || p + 5 + *len > e ? 0 : 5;
}

static u32 be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | p[2] << 8 | p[3]; }

/* parse a DES of UUIDs; returns pointer past it (0 on error); *match = every UUID is in our record */
static const u8 *uuid_pattern(const u8 *p, const u8 *e, int *match)
{
	int len, h = de(p, e, &len), n = 0;
	const u8 *q, *end;

	if (!h || (p[0] >> 3) != 6)
		return 0;
	*match = 1;
	for (q = p + h, end = q + len; q < end; n++) {
		int l, hh = de(q, end, &l), i, hit = 0;
		u32 u = 0xffffffff;

		if (!hh || (q[0] >> 3) != 3)
			return 0;
		if (l == 2) u = q[1] << 8 | q[2];
		else if (l == 4) u = be32(q + 1);
		else if (l == 16 && same(q + 5, base_uuid, 12)) u = be32(q + 1);
		for (i = 0; i < 4; i++)
			if (rec_uuid[i] == u)
				hit = 1;
		if (!hit)
			*match = 0;
		q += hh + l;
	}
	if (n == 0 || n > 12)
		return 0;
	return end;
}

/* parse a DES of attribute ids / ranges into lo/hi pairs */
static const u8 *attr_list(const u8 *p, const u8 *e, u16 *lo, u16 *hi, int *nr)
{
	int len, h = de(p, e, &len);
	const u8 *q, *end;

	*nr = 0;
	if (!h || (p[0] >> 3) != 6)
		return 0;
	for (q = p + h, end = q + len; q < end && *nr < 16; ) {
		if (q[0] == 0x09 && q + 3 <= end) { lo[*nr] = hi[*nr] = q[1] << 8 | q[2]; q += 3; }
		else if (q[0] == 0x0a && q + 5 <= end) { lo[*nr] = q[1] << 8 | q[2]; hi[*nr] = q[3] << 8 | q[4]; q += 5; }
		else return 0;
		(*nr)++;
	}
	return q == end && *nr ? end : 0;
}

/* attribute list DES for our record, filtered by ranges */
static int build_attrs(u8 *o, const u16 *lo, const u16 *hi, int nr)
{
	int i, j, n = 0;
	u8 t[256];

	for (i = 0; i < NREC; i++)
		for (j = 0; j < nr; j++)
			if (rec[i].id >= lo[j] && rec[i].id <= hi[j]) {
				t[n++] = 0x09;
				t[n++] = rec[i].id >> 8;
				t[n++] = rec[i].id & 0xff;
				cpy(t + n, rec[i].v, rec[i].len);
				n += rec[i].len;
				break;
			}
	o[0] = 0x35;
	o[1] = n;
	cpy(o + 2, t, n);
	return n + 2;
}

static int sdp_error(u8 *r, const u8 *q, int code)
{
	r[0] = 0x01; r[1] = q[1]; r[2] = q[2]; r[3] = 0; r[4] = 2; r[5] = 0; r[6] = code;
	return 7;
}

/* continuation: state is 2 bytes = offset into the full attribute byte stream */
static int sdp_chunk(u8 *r, const u8 *q, const u8 *full, int total, int max, const u8 *cont, const u8 *e)
{
	int off = 0, n, k;

	if (cont >= e)
		return sdp_error(r, q, 3);
	if (cont[0] == 2 && cont + 3 <= e)
		off = cont[1] << 8 | cont[2];
	else if (cont[0] != 0)
		return sdp_error(r, q, 5);
	if (off > total)
		return sdp_error(r, q, 5);
	if (max > 600) max = 600;
	if (max < 1) max = 1;
	n = total - off;
	if (n > max) n = max;
	r[0] = q[0] + 1; r[1] = q[1]; r[2] = q[2];
	r[5] = n >> 8; r[6] = n & 0xff;
	cpy(r + 7, full + off, n);
	k = 7 + n;
	if (off + n < total) { r[k++] = 2; r[k++] = (off + n) >> 8; r[k++] = (off + n) & 0xff; }
	else r[k++] = 0;
	r[3] = (k - 5) >> 8; r[4] = (k - 5) & 0xff;
	return k;
}

static int sdp_handle(const u8 *q, int n, u8 *r)
{
	const u8 *p = q + 5, *e = q + n;
	u16 lo[16], hi[16];
	u8 full[300];
	int match = 0, nr, max, total;

	if (n < 5 || (q[3] << 8 | q[4]) != n - 5)
		return n >= 3 ? sdp_error(r, q, 4) : 0;
	switch (q[0]) {
	case 0x02:	/* ServiceSearchRequest */
		if (!(p = uuid_pattern(p, e, &match)) || p + 3 > e)
			return sdp_error(r, q, 3);
		max = p[0] << 8 | p[1];
		if (!max) match = 0;
		r[0] = 0x03; r[1] = q[1]; r[2] = q[2];
		r[5] = 0; r[6] = match; r[7] = 0; r[8] = match;
		n = 9;
		if (match) { r[9] = HANDLE >> 24; r[10] = (HANDLE >> 16) & 0xff; r[11] = (HANDLE >> 8) & 0xff; r[12] = HANDLE & 0xff; n = 13; }
		r[n++] = 0;
		r[3] = 0; r[4] = n - 5;
		return n;
	case 0x04:	/* ServiceAttributeRequest */
		if (p + 6 > e)
			return sdp_error(r, q, 3);
		if (be32(p) != HANDLE)
			return sdp_error(r, q, 2);
		max = p[4] << 8 | p[5];
		if (!(p = attr_list(p + 6, e, lo, hi, &nr)))
			return sdp_error(r, q, 3);
		total = build_attrs(full, lo, hi, nr);
		return sdp_chunk(r, q, full, total, max, p, e);
	case 0x06:	/* ServiceSearchAttributeRequest */
		if (!(p = uuid_pattern(p, e, &match)) || p + 2 > e)
			return sdp_error(r, q, 3);
		max = p[0] << 8 | p[1];
		if (!(p = attr_list(p + 2, e, lo, hi, &nr)))
			return sdp_error(r, q, 3);
		if (match) {
			total = build_attrs(full + 2, lo, hi, nr);
			full[0] = 0x35;
			full[1] = total;
			total += 2;
		} else {
			full[0] = 0x35; full[1] = 0; total = 2;
		}
		return sdp_chunk(r, q, full, total, max, p, e);
	}
	return sdp_error(r, q, 3);
}

/* ---- status JSON via /bin/homtom-status ---- */
static int run_status(char *out, int cap)
{
	long pfd[2], pid, n;
	int t = 0, i, s = -1;
	char buf[3072];		/* HTTP headers plus the JSON, which grew with the "extra" object of homtom-statusd */

	if (sc(SYS_pipe, (long)pfd, 0, 0, 0, 0, 0) < 0)
		return 0;
	pid = sc(SYS_fork, 0, 0, 0, 0, 0, 0);
	if (pid == 0) {
		static const char *argv[] = { STATUS, 0 };
		static const char *envp[] = { "PATH=/bin:/sbin:/usr/bin:/usr/sbin", 0 };
		long nul = sc(SYS_open, (long)"/dev/null", 2, 0, 0, 0, 0);

		sc(SYS_dup2, nul, 0, 0, 0, 0, 0);
		sc(SYS_dup2, nul, 2, 0, 0, 0, 0);
		sc(SYS_dup2, pfd[1], 1, 0, 0, 0, 0);
		for (i = 3; i < 64; i++)
			sc(SYS_close, i, 0, 0, 0, 0, 0);
		sc(SYS_execve, (long)argv[0], (long)argv, (long)envp, 0, 0, 0);
		sc(SYS_exit, 127, 0, 0, 0, 0, 0);
	}
	sc(SYS_close, pfd[1], 0, 0, 0, 0, 0);
	if (pid < 0) { sc(SYS_close, pfd[0], 0, 0, 0, 0, 0); return 0; }
	for (;;) {
		struct pfd p;

		p.fd = (int)pfd[0]; p.ev = POLLIN; p.rev = 0;
		if (sc(SYS_poll, (long)&p, 1, 5000, 0, 0, 0) <= 0) { sc(SYS_kill, pid, 9, 0, 0, 0, 0); logs("homtom-status timed out"); break; }
		if (t >= (int)sizeof buf || (n = sc(SYS_read, pfd[0], (long)(buf + t), sizeof buf - t, 0, 0, 0)) <= 0)
			break;
		t += n;
	}
	sc(SYS_close, pfd[0], 0, 0, 0, 0, 0);
	sc(SYS_wait4, pid, 0, 0, 0, 0, 0);
	/* the script prints HTTP headers first; keep the line that starts with '{' */
	for (i = 0; i < t; i++)
		if (buf[i] == '{' && (i == 0 || buf[i - 1] == '\n')) { s = i; break; }
	if (s < 0)
		return 0;
	for (i = 0; s + i < t && buf[s + i] != '\n' && i < cap - 1; i++)
		out[i] = buf[s + i];
	out[i++] = '\n';
	return i;
}

/* ---- RFCOMM clients ---- */
struct rc { long fd; int n; char line[128]; };
static struct rc rcs[NRC];

static void put(long fd, const char *s, int n)
{
	while (n > 0) {
		long w = sc(SYS_sendto, fd, (long)s, n, MSG_NOSIGNAL, 0, 0);

		if (w <= 0)
			return;
		s += w;
		n -= (int)w;
	}
}
static void puts_(long fd, const char *s) { put(fd, s, slen(s)); }

static void send_status(long fd)
{
	char j[2560];
	int n = run_status(j, sizeof j);

	if (n > 0)
		put(fd, j, n);
	else
		puts_(fd, "{\"error\":\"status unavailable\"}\n");
}

static int is(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == 0 && *b == 0; }

/* start a fixed program detached (no shell, no arguments from the link); the link is paired, authenticated and encrypted */
static void spawn_fixed(const char *path)
{
	long pid = sc(SYS_fork, 0, 0, 0, 0, 0, 0);

	if (pid == 0) {
		static const char *envp[] = { "PATH=/bin:/sbin:/usr/bin:/usr/sbin", 0 };
		const char *argv[2];
		long nul = sc(SYS_open, (long)"/dev/null", 2, 0, 0, 0, 0);
		int i;

		argv[0] = path;
		argv[1] = 0;
		sc(SYS_dup2, nul, 0, 0, 0, 0, 0);
		sc(SYS_dup2, nul, 1, 0, 0, 0, 0);
		sc(SYS_dup2, nul, 2, 0, 0, 0, 0);
		for (i = 3; i < 64; i++)
			sc(SYS_close, i, 0, 0, 0, 0, 0);
		sc(SYS_execve, (long)path, (long)argv, (long)envp, 0, 0, 0);
		sc(SYS_exit, 127, 0, 0, 0, 0, 0);
	}
	if (pid > 0)
		sc(SYS_wait4, pid, 0, 1 /* WNOHANG: the child may take seconds */, 0, 0, 0);
}

/* returns 0 to close */
static int rc_line(struct rc *c)
{
	c->line[c->n] = 0;
	if (c->n == 0)
		return 1;
	if (is(c->line, "status")) send_status(c->fd);
	else if (is(c->line, "ping")) puts_(c->fd, "pong\n");
	else if (is(c->line, "help")) puts_(c->fd, "status | ping | usbkick | reboot | help | quit\n");
	else if (is(c->line, "usbkick")) { logs("link command: usbkick"); spawn_fixed("/bin/homtom-usbkick"); puts_(c->fd, "ok usbkick\n"); }
	else if (is(c->line, "reboot")) { logs("link command: reboot"); spawn_fixed("/bin/reboot"); puts_(c->fd, "ok reboot\n"); }
	else if (is(c->line, "quit")) return 0;
	else puts_(c->fd, "{\"error\":\"unknown command\"}\n");
	return 1;
}

static int rc_input(struct rc *c)
{
	char b[128];
	long n = sc(SYS_recvfrom, c->fd, (long)b, sizeof b, 0, 0, 0), i;

	if (n <= 0)
		return 0;
	for (i = 0; i < n; i++) {
		if (b[i] == '\n' || b[i] == '\r') {
			if (!rc_line(c))
				return 0;
			c->n = 0;
		} else if (c->n < (int)sizeof c->line - 1) {
			c->line[c->n++] = b[i];
		}
	}
	return 1;
}

/* ---- main ---- */
static long listen_l2cap_sdp(void)
{
	u8 a[14];
	long s = sc(SYS_socket, AF_BT, 5, 0, 0, 0, 0);	/* SOCK_SEQPACKET, BTPROTO_L2CAP */

	if (s < 0) return s;
	zero(a, sizeof a);
	a[0] = AF_BT; a[2] = 1;				/* l2_family, l2_psm = 1 (LE), BDADDR_ANY, BR/EDR */
	if (sc(SYS_bind, s, (long)a, sizeof a, 0, 0, 0) < 0 || sc(SYS_listen, s, 4, 0, 0, 0, 0) < 0)
		return -1;
	return s;
}

static long listen_rfcomm(void)
{
	u8 a[10], sec[2];
	long s = sc(SYS_socket, AF_BT, 1, 3, 0, 0, 0), r;	/* SOCK_STREAM, BTPROTO_RFCOMM */

	if (s < 0) return s;
	sec[0] = opt_low ? 1 : 2;			/* BT_SECURITY_LOW / MEDIUM (auth + encryption) */
	sec[1] = 0;
	r = sc(SYS_setsockopt, s, 274, 4, (long)sec, 2, 0);	/* SOL_BLUETOOTH, BT_SECURITY */
	if (r < 0) { ls("btd: rfcomm BT_SECURITY err "); ld(r); lend(); }
	zero(a, sizeof a);
	a[0] = AF_BT; a[8] = CHANNEL;			/* rc_family, BDADDR_ANY, rc_channel */
	if (sc(SYS_bind, s, (long)a, sizeof a, 0, 0, 0) < 0 || sc(SYS_listen, s, 2, 0, 0, 0, 0) < 0)
		return -1;
	return s;
}

static int dev_flags(u32 *flags, u8 *bdaddr)
{
	u8 di[96];

	zero(di, sizeof di);
	di[0] = dev;
	if (sc(SYS_ioctl, hci, 0x800448d3, (long)di, 0, 0, 0) < 0)	/* HCIGETDEVINFO */
		return -1;
	*flags = di[16] | di[17] << 8 | di[18] << 16 | (u32)di[19] << 24;
	cpy(bdaddr, di + 10, 6);
	return 0;
}

int main_c(long *sp)
{
	int argc = (int)sp[0], i, n;
	char **argv = (char **)(sp + 1);
	u32 filt[4], flags, last_check = 0;
	u8 addr[6], bd[6];
	long sdp, rfc, sdpc[NSDP];
	struct pfd p[3 + NSDP + NRC];

	for (i = 1; i < argc; i++) {
		if (is(argv[i], "-o")) opt_open = 1;
		else if (is(argv[i], "-l")) opt_low = 1;
		else if (is(argv[i], "-d") && i + 1 < argc) dev = argv[++i][0] - '0';
		else { logs("usage: homtom-btd [-o] [-l] [-d N]"); return 2; }
	}
	hci = sc(SYS_socket, AF_BT, 3, 1, 0, 0, 0);	/* SOCK_RAW, BTPROTO_HCI */
	if (hci < 0) { logs("no AF_BLUETOOTH (bluetooth.ko loaded?)"); return 1; }
	if (dev_flags(&flags, bd) < 0) { logs("HCIGETDEVINFO failed: no such hci device"); return 1; }
	ls("btd: hci"); ld(dev); ls(" "); la(bd); ls(" flags "); lx(flags, 8); lend();
	if (!(flags & 1)) { logs("hci not UP in the kernel (run hcictl up first)"); return 1; }

	filt[0] = 1 << 4;				/* HCI_EVENT_PKT only */
	filt[1] = 1u << 0x03 | 1u << 0x04 | 1u << 0x05 | 1u << 0x06 | 1u << 0x08	/* bit index = event code & 63 */
		| 1u << 0x0e | 1u << 0x0f | 1u << 0x16 | 1u << 0x17 | 1u << 0x18;
	filt[2] = 0;					/* device removal shows up as POLLERR (EPIPE), not as event 0xfd on a bound socket */
	filt[3] = 0;
	sc(SYS_setsockopt, hci, 0, 2, (long)filt, 16, 0);	/* SOL_HCI, HCI_FILTER */
	zero(addr, sizeof addr);
	addr[0] = AF_BT; addr[2] = dev;			/* sockaddr_hci { family, dev, channel RAW } */
	if (sc(SYS_bind, hci, (long)addr, 6, 0, 0, 0) < 0) { logs("bind hci failed"); return 1; }

	{
		u8 v = opt_open ? 0 : 1;

		hci_cmd(0x0c20, &v, 1);			/* Write_Authentication_Enable: every ACL link must pair/authenticate */
	}

	sdp = listen_l2cap_sdp();
	if (sdp < 0) { ls("btd: SDP listen on PSM 1 failed "); ld(sdp); lend(); return 1; }
	rfc = listen_rfcomm();
	if (rfc < 0) { ls("btd: RFCOMM listen failed "); ld(rfc); ls(" (rfcomm.ko loaded?)"); lend(); return 1; }
	for (i = 0; i < NSDP; i++) sdpc[i] = -1;
	for (i = 0; i < NRC; i++) rcs[i].fd = -1;
	ls("btd: ready, SDP PSM 1, RFCOMM channel 1, auth "); ls(opt_open ? "off" : "on");
	ls(", rfcomm security "); ls(opt_low ? "low" : "medium"); lend();

	for (;;) {
		int np = 0;

		p[np].fd = (int)hci; p[np].ev = POLLIN; p[np++].rev = 0;
		p[np].fd = (int)sdp; p[np].ev = POLLIN; p[np++].rev = 0;
		p[np].fd = (int)rfc; p[np].ev = POLLIN; p[np++].rev = 0;
		for (i = 0; i < NSDP; i++) { p[np].fd = (int)sdpc[i]; p[np].ev = POLLIN; p[np++].rev = 0; }
		for (i = 0; i < NRC; i++) { p[np].fd = (int)rcs[i].fd; p[np].ev = POLLIN; p[np++].rev = 0; }
		n = (int)sc(SYS_poll, (long)p, np, 1000, 0, 0, 0);	/* wake every second for the pairing window */
		if (mono_s() != last_check) {
			last_check = mono_s();
			pair_check();
		}
		if (n <= 0)
			continue;

		if (p[0].rev & (POLLERR | POLLHUP)) { logs("hci socket error"); return 3; }
		if ((p[0].rev & POLLIN) && hci_event() < 0)
			return 3;

		if (p[1].rev & POLLIN) {
			u8 ra[14];
			long al = sizeof ra, c = sc(SYS_accept, sdp, (long)ra, (long)&al, 0, 0, 0);

			if (c >= 0) {
				for (i = 0; i < NSDP && sdpc[i] >= 0; i++)
					;
				if (i == NSDP) sc(SYS_close, c, 0, 0, 0, 0, 0);
				else { sdpc[i] = c; ls("btd: sdp client "); la(ra + 2); lend(); }
			}
		}
		if (p[2].rev & POLLIN) {
			u8 ra[10];
			long al = sizeof ra, c = sc(SYS_accept, rfc, (long)ra, (long)&al, 0, 0, 0);

			if (c >= 0) {
				for (i = 0; i < NRC && rcs[i].fd >= 0; i++)
					;
				if (i == NRC) { puts_(c, "{\"error\":\"busy\"}\n"); sc(SYS_close, c, 0, 0, 0, 0, 0); }
				else {
					rcs[i].fd = c; rcs[i].n = 0;
					ls("btd: rfcomm client "); la(ra + 2); lend();
					send_status(c);
				}
			}
		}
		for (i = 0; i < NSDP; i++) {
			struct pfd *q = &p[3 + i];

			if (sdpc[i] < 0 || !q->rev)
				continue;
			{
				u8 rq[700], rs[700];
				long n = sc(SYS_recvfrom, sdpc[i], (long)rq, sizeof rq, 0, 0, 0);
				int k;

				if (n <= 0) { sc(SYS_close, sdpc[i], 0, 0, 0, 0, 0); sdpc[i] = -1; continue; }
				k = sdp_handle(rq, (int)n, rs);
				ls("btd: sdp pdu "); lx(rq[0], 2); ls(" -> "); lx(k ? rs[0] : 0, 2); ls(" len "); ld(k); lend();
				if (k > 0)
					sc(SYS_sendto, sdpc[i], (long)rs, k, MSG_NOSIGNAL, 0, 0);
			}
		}
		for (i = 0; i < NRC; i++) {
			struct pfd *q = &p[3 + NSDP + i];

			if (rcs[i].fd < 0 || !q->rev)
				continue;
			if (!rc_input(&rcs[i])) {
				logs("rfcomm client closed");
				sc(SYS_close, rcs[i].fd, 0, 0, 0, 0, 0);
				rcs[i].fd = -1;
			}
		}
	}
}

__asm__(".globl _start\n_start:\n mov r0, sp\n bl main_c\n mov r7, #1\n svc 0\n");
