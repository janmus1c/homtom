/* hcictl-info [N]: print the kernel's view of hciN via HCIGETDEVINFO (no traffic to the controller).
 * Freestanding ARMv4T. Meant to become `hcictl info` (same sc() helper, one more argv branch).
 * Output: "hci0 00:02:5B:.. flags 00000019 UP PSCAN ISCAN acl_mtu 0310:0008 features ff ff 8f fe .. evt_rx N cmd_tx N err_rx N"
 * flags bits (include/net/bluetooth/hci.h): 0 UP 1 INIT 2 RUNNING 3 PSCAN 4 ISCAN 5 AUTH 6 ENCRYPT 7 INQUIRY 8 RAW 9 RESET */
typedef unsigned int u32;
typedef unsigned char u8;

static long sc(long n, long a, long b, long c)
{
	register long r7 __asm__("r7") = n;
	register long r0 __asm__("r0") = a;
	register long r1 __asm__("r1") = b;
	register long r2 __asm__("r2") = c;

	__asm__ volatile("svc 0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2) : "memory");
	return r0;
}

static char o[512];
static int k;
static void c(char ch) { if (k < 510) o[k++] = ch; }
static void s(const char *t) { while (*t) c(*t++); }
static void x(u32 v, int d) { while (d--) c("0123456789abcdef"[(v >> (d * 4)) & 15]); }
static void dec(u32 v)
{
	static const u32 p[] = { 1000000000, 100000000, 10000000, 1000000, 100000, 10000, 1000, 100, 10, 1 };
	int i, on = 0;

	for (i = 0; i < 10; i++) {
		int d = 0;

		while (v >= p[i]) { v -= p[i]; d++; }
		if (d || on || i == 9) { c('0' + d); on = 1; }
	}
}
static u32 le32(const u8 *b) { return b[0] | b[1] << 8 | b[2] << 16 | (u32)b[3] << 24; }

int main_c(long *sp)
{
	static const char *names[] = { "UP", "INIT", "RUNNING", "PSCAN", "ISCAN", "AUTH", "ENCRYPT", "INQUIRY", "RAW", "RESET" };
	char **argv = (char **)(sp + 1);
	u8 di[96];
	long fd;
	u32 f;
	int i;

	for (i = 0; i < 96; i++)
		di[i] = 0;
	if (sp[0] > 1)
		di[0] = argv[1][0] - '0';
	fd = sc(281, 31, 3, 1);				/* socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI), unbound */
	if (fd < 0)
		return 1;
	if (sc(54, fd, 0x800448d3, (long)di) < 0)	/* HCIGETDEVINFO */
		return 2;
	/* struct hci_dev_info: dev_id@0 name@2 bdaddr@10 flags@16 type@20 features@21 pkt_type@32 link_policy@36
	 * link_mode@40 acl_mtu@44 acl_pkts@46 sco_mtu@48 sco_pkts@50 stat@52 {err_rx err_tx cmd_tx evt_rx acl_tx acl_rx ...} */
	for (i = 2; i < 10 && di[i]; i++)
		c(di[i]);
	c(' ');
	for (i = 15; i >= 10; i--) { x(di[i], 2); if (i > 10) c(':'); }
	f = le32(di + 16);
	s(" flags "); x(f, 8);
	for (i = 0; i < 10; i++)
		if (f & (1u << i)) { c(' '); s(names[i]); }
	s(" acl_mtu "); x(di[44] | di[45] << 8, 4); c(':'); x(di[46] | di[47] << 8, 4);
	s(" link_mode "); x(le32(di + 40), 8);
	s(" features");
	for (i = 21; i < 29; i++) { c(' '); x(di[i], 2); }
	s(" err_rx "); dec(le32(di + 52));
	s(" err_tx "); dec(le32(di + 56));
	s(" cmd_tx "); dec(le32(di + 60));
	s(" evt_rx "); dec(le32(di + 64));
	s(" acl_tx "); dec(le32(di + 68));
	s(" acl_rx "); dec(le32(di + 72));
	c('\n');
	sc(4, 1, (long)o, k);
	return f & 1 ? 0 : 3;				/* exit 0 only when the kernel says UP */
}

__asm__(".globl _start\n_start:\n mov r0, sp\n bl main_c\n mov r7, #1\n svc 0\n");
