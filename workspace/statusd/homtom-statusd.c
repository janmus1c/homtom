/* homtom-statusd [PORT [IPV4 [passive]]]: one daemon for the status endpoint and the GPS reader of the GO 630 (S3C2443).
 * Replaces `tcpsvd 8080 homtom-status` (about 45 forks and 2.2 s per request) and homtom-gpsd (docs/PERF.md sections 4, 5).
 *
 * - One mmap of /dev/mem for GPIO (0x56000000) and ADC (0x58000000). The ADC is shared with homtom-touch only through the request/ack protocol below.
 * - Every 5 s: raise GPL14 (AIN4 reference) and LX_EN (GPJ1), wait SETTLE_MS, ask homtom-touch for the ADC through
 *   /tmp/adc.req and /tmp/adc.ack (at most 40 ms, none when no touch tool runs), read battery (mux 0), reference (mux 4),
 *   light (mux 2) five times each, release, drop both pins again. Each read discards the first conversion, waits for
 *   ECFLG and is dropped if ADCMUX changed meanwhile; the medians go through the filter in statusd_core.h
 *   (docs/ADC_ARBITRATION.md).
 * - Reads the SiRF III NMEA on /dev/ttySAC2 (4800 baud, raw, read only); the GPS power and reset pins stay with
 *   `homtom-hw gps-on`. Keeps GGA/RMC in /tmp/gga, /tmp/rmc (CRLF, as homtom-gpsd wrote them), /tmp/gps_ts, and the UI file.
 * - HTTP on TCP 8080, one request at a time, Connection: close, JSON from the cache. GPIO inputs are read at request time
 *   (reads only); no hardware write happens on the request path.
 * - Per request also: /proc/loadavg, the head of /proc/meminfo, the /sys/class/bluetooth directory and /tmp/touch.last
 *   (written by homtom-ui-start), one read each, no forks.
 * - /tmp/ui.extra (key=value lines from the tool-page wrappers, docs/SCREEN_TOOLS.md) is re-read for every UI file and
 *   request: valid lines are appended to /tmp/homtom-ui.txt and returned under "extra" in the JSON.
 * - `passive` (third argument) skips the ADC sampler, the GPS reader and every file write, so a test instance can run
 *   on another port beside the live daemon without a second ADC owner or a second ttySAC2 reader.
 * - poll() multiplexes listener, client and the GPS fd. No threads, no signals handlers, SIGPIPE ignored, never exits on errors.
 *
 * Register writes go through hw_wr(): an allowlist of exactly three masked writes, and the GPJ/GPL hazards from
 * docs/HARDWARE_MAP.md (GPJDAT bits 10/14, GPJCON pins 10/14, GPLCON pins 0-9) are rejected explicitly.
 *
 * Freestanding (raw EABI syscalls, no libc), ARMv4T; style follows tools/devtools/devmem.c. Build: build.sh. */
#include "statusd_core.h"

#define SETTLE_MS	250	/* AIN4 reference and light sensor settle; the shell used 1000. Hypothesis: to be validated on hardware */
#define SAMPLE_MS	5000
#define UI_MS		1000
#define CLIENT_MS	2000

#define GPIO_PA		0x56000000u
#define ADC_PA		0x58000000u
#define GPFDAT		0x54
#define GPGDAT		0x64
#define GPJCON		0xD0
#define GPJDAT		0xD4
#define GPLCON		0xF0
#define GPLDAT		0xF4
#define ADCCON		0
#define ADCDAT0		3			/* word index */
#define ADCMUX		6
#define ADC_START	((1u << 14) | (49u << 6) | 1u)
#define ADC_ECFLG	0x8000u

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

#define SYS_exit	1
#define SYS_read	3
#define SYS_write	4
#define SYS_open	5
#define SYS_close	6
#define SYS_unlink	10
#define SYS_getpid	20
#define SYS_access	33
#define SYS_kill	37
#define SYS_rename	38
#define SYS_ioctl	54
#define SYS_poll	168
#define SYS_rt_sigaction 174
#define SYS_mmap2	192
#define SYS_stat64	195
#define SYS_fstat64	197
#define SYS_getdents64	217
#define SYS_clock_gettime 263
#define SYS_socket	281
#define SYS_bind	282
#define SYS_listen	284
#define SYS_send	289
#define SYS_shutdown	293
#define SYS_setsockopt	294
#define SYS_accept4	366

#define ESRCH		3
#define EINTR		4
#define EAGAIN		11
#define EEXIST		17
#define O_NONBLOCK	0x800
#define POLLIN		1
#define POLLERR		8
#define POLLHUP		16

struct pfd {
	int fd;
	short events, revents;
};

static volatile u32 *gpio, *adc;
static struct sd_state st;
static struct sd_lb lb;
static int lfd = -1, cfd = -1, gfd = -1;
static char req[512];
static int rl;
static u32 now, d_listen, d_adc, d_ui, d_gps, d_client, adc_t0;
static int adc_phase;
static u32 port = 8080, bindaddr;
static int passive;			/* no ADC, GPS or file writes: a second instance for testing beside the live one */
static char out[2304], body[2048], ui[1536];

static int due(u32 deadline)
{
	return (int)(deadline - now) <= 0;
}

static void update_now(void)
{
	u32 ts[2];			/* tv_sec, tv_nsec (32-bit timespec) */

	if (sc(SYS_clock_gettime, 7, (long)ts, 0, 0, 0, 0) < 0)	/* CLOCK_BOOTTIME */
		sc(SYS_clock_gettime, 1, (long)ts, 0, 0, 0, 0);
	now = ts[0] * 1000u + ts[1] / 1000000u;
	st.uptime_s = ts[0];
}

static int slen(const char *s)
{
	int n = 0;

	while (s[n])
		n++;
	return n;
}

static void put_all(int fd, const char *s, int n)
{
	while (n > 0) {
		long r = sc(SYS_write, fd, (long)s, n, 0, 0, 0);

		if (r == -EINTR)
			continue;
		if (r <= 0)
			return;
		s += r;
		n -= (int)r;
	}
}

/* write to path.tmp then rename: readers never see a partial file */
static void write_atomic(const char *path, const char *tmp, const char *data, int n)
{
	long fd = sc(SYS_open, (long)tmp, 1 | 0x40 | 0x200, 0644, 0, 0, 0);	/* O_WRONLY|O_CREAT|O_TRUNC */

	if (fd < 0)
		return;
	put_all((int)fd, data, n);
	sc(SYS_close, fd, 0, 0, 0, 0, 0);
	sc(SYS_rename, (long)tmp, (long)path, 0, 0, 0, 0);
}

/* ---- hardware ---- */
static volatile u32 *map(long fd, u32 pa)
{
	long m = sc(SYS_mmap2, 0, 4096, 3, 1, fd, pa >> 12);

	return (m < 0 && m > -4096) ? 0 : (volatile u32 *)m;
}

static void hw_open(void)
{
	long fd;

	if (gpio && adc)
		return;
	fd = sc(SYS_open, (long)"/dev/mem", 2 | 0x101000, 0, 0, 0, 0);	/* O_RDWR|O_SYNC */
	if (fd < 0)
		return;
	if (!gpio)
		gpio = map(fd, GPIO_PA);
	if (!adc)
		adc = map(fd, ADC_PA);
	sc(SYS_close, fd, 0, 0, 0, 0, 0);
}

/* Masked write to a GPIO register. Allowlist: GPL14 in GPLDAT (AIN4_PWR), LX_EN GPJ1 as output in GPJCON, GPJ1 in GPJDAT.
 * Hazards (docs/HARDWARE_MAP.md): GPJDAT bits 10/14 = PWR_RST and moviNAND power, GPJCON pins 10/14, GPLCON pins 0-9. */
static int hw_wr(u32 off, u32 mask, u32 val)
{
	if (!gpio)
		return -1;
	if ((off == GPJDAT && (mask & 0x4400)) || (off == GPJCON && (mask & 0x30300000)) || (off == GPLCON && (mask & 0xfffff)))
		return -1;
	if (!((off == GPLDAT && mask == 0x4000) || (off == GPJCON && mask == 0xC) || (off == GPJDAT && mask == 0x2)))
		return -1;
	gpio[off / 4] = (gpio[off / 4] & ~mask) | (val & mask);
	return 0;
}

/* one 10-bit sample of ADC mux channel; the first conversion after a mux change is discarded. -1 on a timeout or when
 * another ADC user switched ADCMUX meanwhile (the value would belong to its channel). */
static int adc_read(u32 mux)
{
	int k, j, v = -1;

	if (!adc)
		return -1;
	for (k = 0; k < 2; k++) {
		adc[ADCMUX] = mux;
		(void)adc[ADCDAT0];
		adc[ADCCON] = ADC_START;
		for (j = 0; j < 4; j++)		/* ECFLG still shows the previous conversion until the start takes effect */
			(void)adc[ADCCON];
		for (j = 0; j < 200000; j++)
			if (adc[ADCCON] & ADC_ECFLG)
				break;
		if (j == 200000) {
			st.adc_bad++;
			return -1;
		}
		v = (int)(adc[ADCDAT0] & 0x3ff);
	}
	if ((adc[ADCMUX] & 0xf) != mux) {
		st.adc_bad++;
		return -1;
	}
	return v;
}

/* ---- ADC arbitration with homtom-touch (docs/ADC_ARBITRATION.md) ---- */
#define ADC_REQ		"/tmp/adc.req"
#define ADC_ACK		"/tmp/adc.ack"
#define ADC_HOLDER	"/tmp/adc.touch"
#define ARB_WAIT_MS	40
#define ARB_STEP_MS	2
#define ARB_BUSY_MS	250

static int read_small(const char *path, char *buf, int cap, u32 *mtime);
static void read_inputs(void);
static u32 arb_t0;
static int arb_pid, arb_req;

/* 1 when the file exists and is a leftover (older than 2 s or from the future) */
static int arb_file_stale(const char *path)
{
	u32 sb[26], ts[2];			/* struct stat64: st_mtime at byte 80, st_mtime_nsec at 84 */

	if (sc(SYS_stat64, (long)path, (long)sb, 0, 0, 0, 0) != 0)
		return 0;
	if (sc(SYS_clock_gettime, 0, (long)ts, 0, 0, 0, 0) != 0)
		return 0;
	return sd_arb_stale(sd_age_ms(ts[0], ts[1], sb[20], sb[21]));
}

/* pid of a live touch tool from /tmp/adc.touch, else 0 (a dead pid's file is removed) */
static int arb_holder(void)
{
	char b[16];
	int n = read_small(ADC_HOLDER, b, sizeof(b) - 1, 0), i, pid = 0;

	for (i = 0; i < n && b[i] >= '0' && b[i] <= '9' && pid < 1000000; i++)
		pid = pid * 10 + (b[i] - '0');
	if (pid <= 1)
		return 0;
	if (sc(SYS_kill, pid, 0, 0, 0, 0, 0) == -ESRCH) {
		sc(SYS_unlink, (long)ADC_HOLDER, 0, 0, 0, 0, 0);
		return 0;
	}
	return pid;
}

/* 1: wait for the acknowledgement, 0: sample now, -1: another request is active, retry later */
static int arb_begin(void)
{
	struct sd_w w;
	char b[16];
	long fd = -EEXIST;
	int k;

	sc(SYS_unlink, (long)ADC_ACK, 0, 0, 0, 0, 0);	/* only statusd requests: an acknowledgement now is left over */
	for (k = 0; k < 2 && fd == -EEXIST; k++) {
		fd = sc(SYS_open, (long)ADC_REQ, 1 | 0x40 | 0x80, 0644, 0, 0, 0);	/* O_WRONLY|O_CREAT|O_EXCL */
		if (fd == -EEXIST && !k) {
			if (!arb_file_stale(ADC_REQ))
				break;
			sc(SYS_unlink, (long)ADC_REQ, 0, 0, 0, 0, 0);
		}
	}
	if (fd == -EEXIST)
		return -1;
	arb_req = fd >= 0;			/* without /tmp the filter is the only protection */
	if (arb_req) {
		w.p = b;
		w.e = b + sizeof(b);
		sd_wu(&w, (u32)sc(SYS_getpid, 0, 0, 0, 0, 0, 0));
		sd_ws(&w, "\n");
		put_all((int)fd, b, (int)(w.p - b));
		sc(SYS_close, fd, 0, 0, 0, 0, 0);
	}
	arb_t0 = now;
	arb_pid = arb_holder();
	return arb_req && arb_pid;
}

static void arb_end(void)
{
	u32 w = now - arb_t0;

	st.arb_wait_ms = w;
	if (w > st.arb_wait_max)
		st.arb_wait_max = w;
	if (arb_req) {
		sc(SYS_unlink, (long)ADC_ACK, 0, 0, 0, 0, 0);
		sc(SYS_unlink, (long)ADC_REQ, 0, 0, 0, 0, 0);
	}
	arb_req = 0;
}

/* SD_ADC_N two-conversion reads per channel while the request is held, then the filter */
static void adc_sample(void)
{
	static const u32 mx[3] = { 0, 4, 2 };	/* battery, reference, light */
	int s[3][SD_ADC_N], c, i;

	for (c = 0; c < 3; c++)
		for (i = 0; i < SD_ADC_N; i++)
			s[c][i] = adc_read(mx[c]);
	arb_end();
	hw_wr(GPLDAT, 0x4000, 0);
	hw_wr(GPJDAT, 0x2, 0);
	sd_adc_cycle(&st, s);
	adc_phase = 0;
	d_adc = adc_t0 + SAMPLE_MS;
	if (due(d_adc))
		d_adc = now + SAMPLE_MS;
	read_inputs();
}

static void read_inputs(void)
{
	if (!gpio) {
		st.charging = st.ext_power = st.key = -1;
		return;
	}
	st.charging = !((gpio[GPGDAT / 4] >> 1) & 1);	/* GPG1 low = charging */
	st.ext_power = !((gpio[GPFDAT / 4] >> 2) & 1);	/* GPF2 low = external power */
	st.key = !(gpio[GPFDAT / 4] & 1);		/* GPF0 low = pressed */
}

static void adc_step(void)
{
	if (adc_phase == 0) {
		hw_open();
		if (!gpio || !adc) {
			d_adc = now + SAMPLE_MS;
			return;
		}
		hw_wr(GPLDAT, 0x4000, 0x4000);		/* AIN4_PWR high */
		hw_wr(GPJCON, 0xC, 0x4);		/* GPJ1 output */
		hw_wr(GPJDAT, 0x2, 0x2);		/* LX_EN high */
		adc_t0 = now;
		adc_phase = 1;
		d_adc = now + SETTLE_MS;
		return;
	}
	if (adc_phase == 1) {
		int r = arb_begin();

		if (r < 0) {
			st.arb_busy++;
			d_adc = now + ARB_BUSY_MS;
			return;
		}
		if (r == 0) {
			st.arb_none++;
			adc_sample();
			return;
		}
		adc_phase = 2;
		d_adc = now + ARB_STEP_MS;
		return;
	}
	if (sc(SYS_access, (long)ADC_ACK, 0, 0, 0, 0, 0) == 0)
		st.arb_ack++;
	else if ((u32)(now - arb_t0) >= ARB_WAIT_MS)
		st.arb_timeout++;
	else if (sc(SYS_kill, arb_pid, 0, 0, 0, 0, 0) == -ESRCH)
		st.arb_none++;
	else {
		d_adc = now + ARB_STEP_MS;
		return;
	}
	adc_sample();
}

/* ---- GPS ---- */
static void gps_open(void)
{
	u32 t[9];
	u8 *b = (u8 *)t;
	int i;
	long fd = sc(SYS_open, (long)"/dev/ttySAC2", 0 | 0x100 | O_NONBLOCK, 0, 0, 0, 0);	/* O_RDONLY|O_NOCTTY|O_NONBLOCK */

	if (fd < 0) {
		d_gps = now + 5000;
		return;
	}
	for (i = 0; i < 36; i++)
		b[i] = 0;
	t[2] = 0xC | 0x30 | 0x80 | 0x800;	/* B4800 | CS8 | CREAD | CLOCAL, everything else off = raw */
	b[17 + 6] = 1;				/* c_cc[VMIN] */
	sc(SYS_ioctl, fd, 0x5402, (long)t, 0, 0, 0);	/* TCSETS */
	gfd = (int)fd;
	lb.n = lb.over = 0;
}

static void gps_close(void)
{
	sc(SYS_close, gfd, 0, 0, 0, 0, 0);
	gfd = -1;
	d_gps = now + 1000;
}

static void gps_write(int r)
{
	char t[SD_NMEA_MAX + 4];
	const char *s = r == 1 ? st.gga : st.rmc;
	int n = slen(s);
	int i;

	for (i = 0; i < n; i++)
		t[i] = s[i];
	t[n++] = '\r';
	t[n++] = '\n';
	write_atomic(r == 1 ? "/tmp/gga" : "/tmp/rmc", r == 1 ? "/tmp/gga.n" : "/tmp/rmc.n", t, n);
	if (r == 1 && st.fix_valid && st.last_fix_up == st.uptime_s) {
		struct sd_w w;
		char u[16];

		w.p = u;
		w.e = u + sizeof(u);
		sd_wu(&w, st.uptime_s);
		sd_ws(&w, "\n");
		write_atomic("/tmp/gps_ts", "/tmp/gps_ts.n", u, (int)(w.p - u));
	}
}

static void gps_readable(void)
{
	char buf[256];
	int pass, i;

	for (pass = 0; pass < 4; pass++) {
		long n = sc(SYS_read, gfd, (long)buf, sizeof(buf), 0, 0, 0);

		if (n == -EINTR)
			continue;
		if (n == -EAGAIN)
			return;
		if (n <= 0) {
			gps_close();
			return;
		}
		for (i = 0; i < n; i++)
			if (sd_lb_push(&lb, buf[i])) {
				int r = sd_nmea_feed(&st, lb.buf, lb.n, st.uptime_s);

				lb.n = 0;
				if (r)
					gps_write(r);
			}
	}
}

/* one read of at most SD_EXTRA_MAX bytes; writers replace the file by rename, and a last line without '\n' is ignored */
static void extra_refresh(void)
{
	char b[SD_EXTRA_MAX];
	int n = read_small("/tmp/ui.extra", b, sizeof(b), 0);

	sd_parse_extra(&st, b, n > 0 ? n : 0);
}

/* ---- UI file ---- */
static void ui_write(void)
{
	int n;

	read_inputs();
	extra_refresh();
	n = sd_ui(&st, ui, sizeof(ui));
	write_atomic("/tmp/homtom-ui.txt", "/tmp/homtom-ui.txt.new", ui, n);
}

/* ---- system fields: one open/read/close per source and request ---- */
static int read_small(const char *path, char *buf, int cap, u32 *mtime)
{
	long fd = sc(SYS_open, (long)path, 0, 0, 0, 0, 0);
	long n;

	if (fd < 0)
		return -1;
	if (mtime) {
		u32 sb[26];			/* struct stat64 (ARM EABI, 104 bytes); st_mtime at byte 80 */

		*mtime = 0;
		if (sc(SYS_fstat64, fd, (long)sb, 0, 0, 0, 0) == 0)
			*mtime = sb[20];
	}
	do
		n = sc(SYS_read, fd, (long)buf, cap, 0, 0, 0);
	while (n == -EINTR);
	sc(SYS_close, fd, 0, 0, 0, 0, 0);
	return n < 0 ? -1 : (int)n;
}

static void bt_scan(void)
{
	char d[512];
	long fd = sc(SYS_open, (long)"/sys/class/bluetooth", 0x4000, 0, 0, 0, 0);	/* O_RDONLY|O_DIRECTORY */
	int pass;

	if (fd < 0) {
		st.bt_up = st.bt_links = fd == -2 ? 0 : -1;	/* ENOENT: bluetooth.ko not loaded */
		return;
	}
	st.bt_up = st.bt_links = 0;
	for (pass = 0; pass < 4; pass++) {
		long n = sc(SYS_getdents64, fd, (long)d, sizeof(d), 0, 0, 0);
		int o = 0;

		if (n <= 0)
			break;
		while (o + 19 < n) {	/* linux_dirent64: u64 ino, s64 off, u16 reclen, u8 type, name */
			int rl = (u8)d[o + 16] | ((u8)d[o + 17] << 8);

			if (rl <= 0)
				break;
			sd_bt_entry(&st, d + o + 19);
			o += rl;
		}
	}
	sc(SYS_close, fd, 0, 0, 0, 0, 0);
}

static void sys_refresh(void)
{
	char b[256];
	int n;
	u32 mt, ts[2];

	sd_sys_unknown(&st);
	n = read_small("/proc/loadavg", b, sizeof(b), 0);
	if (n > 0)
		sd_parse_loadavg(&st, b, n);
	n = read_small("/proc/meminfo", b, sizeof(b), 0);	/* MemTotal, MemFree, MemAvailable are the first lines */
	if (n > 0)
		sd_parse_meminfo(&st, b, n);
	bt_scan();
	n = read_small("/tmp/touch.last", b, 63, &mt);
	if (n > 0 && sc(SYS_clock_gettime, 0, (long)ts, 0, 0, 0, 0) == 0)	/* CLOCK_REALTIME, same clock as the mtime */
		sd_parse_touch(&st, b, n, mt, ts[0]);
}

/* ---- HTTP ---- */
static void listen_open(void)
{
	long fd = sc(SYS_socket, 2, 1, 0, 0, 0, 0);
	u32 sa[4];
	int one = 1;

	if (fd < 0) {
		d_listen = now + 2000;
		return;
	}
	sc(SYS_setsockopt, fd, 1, 2, (long)&one, 4, 0);	/* SOL_SOCKET, SO_REUSEADDR */
	sa[0] = 2u | ((((port & 0xff) << 8) | ((port >> 8) & 0xff)) << 16);	/* AF_INET, port in network order */
	sa[1] = bindaddr;
	sa[2] = sa[3] = 0;
	if (sc(SYS_bind, fd, (long)sa, 16, 0, 0, 0) < 0 || sc(SYS_listen, fd, 4, 0, 0, 0, 0) < 0) {
		sc(SYS_close, fd, 0, 0, 0, 0, 0);
		d_listen = now + 2000;
		return;
	}
	lfd = (int)fd;
}

static void client_close(void)
{
	sc(SYS_close, cfd, 0, 0, 0, 0, 0);
	cfd = -1;
	rl = 0;
}

static void respond(void)
{
	struct sd_w w;
	int bl, i, tries = 0;
	char *p;
	long r;

	read_inputs();
	sys_refresh();
	extra_refresh();
	bl = sd_json(&st, body, sizeof(body));
	w.p = out;
	w.e = out + sizeof(out);
	sd_ws(&w, "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: ");
	sd_wu(&w, (u32)bl);
	sd_ws(&w, "\r\n\r\n");
	for (i = 0; i < bl && w.p < w.e; i++)
		*w.p++ = body[i];
	i = (int)(w.p - out);
	p = out;
	while (i > 0 && tries < 8) {
		r = sc(SYS_send, cfd, (long)p, i, 0x4000, 0, 0);	/* MSG_NOSIGNAL */
		if (r == -EINTR || r == -EAGAIN) {
			tries++;
			continue;
		}
		if (r <= 0)
			break;
		p += r;
		i -= (int)r;
	}
	sc(SYS_shutdown, cfd, 1, 0, 0, 0, 0);
	client_close();
}

static int req_done(void)
{
	int i;

	for (i = 0; i + 1 < rl; i++)
		if (req[i] == '\n' && (req[i + 1] == '\n' || (req[i + 1] == '\r' && i + 2 < rl && req[i + 2] == '\n')))
			return 1;
	return 0;
}

static void client_readable(void)
{
	long n = sc(SYS_read, cfd, (long)(req + rl), sizeof(req) - (u32)rl, 0, 0, 0);

	if (n == -EINTR || n == -EAGAIN)
		return;
	if (n < 0 || (n == 0 && rl == 0)) {
		client_close();
		return;
	}
	if (n == 0) {
		respond();
		return;
	}
	rl += (int)n;
	if (req_done() || rl >= (int)sizeof(req))
		respond();
}

static void accept_one(void)
{
	long fd = sc(SYS_accept4, lfd, 0, 0, O_NONBLOCK, 0, 0);

	if (fd < 0)
		return;
	cfd = (int)fd;
	rl = 0;
	d_client = now + CLIENT_MS;
	client_readable();
}

static u32 ip4(const char *s)
{
	u32 a[4] = {0, 0, 0, 0}, i = 0;

	for (; *s; s++) {
		if (*s == '.' && i < 3)
			i++;
		else if (*s >= '0' && *s <= '9')
			a[i] = a[i] * 10 + (u32)(*s - '0');
	}
	return (a[0] & 255) | ((a[1] & 255) << 8) | ((a[2] & 255) << 16) | ((a[3] & 255) << 24);
}

static u32 num(const char *s)
{
	u32 v = 0;

	for (; *s >= '0' && *s <= '9'; s++)
		v = v * 10 + (u32)(*s - '0');
	return v;
}

int main_c(long *sp)
{
	int argc = (int)sp[0];
	char **argv = (char **)(sp + 1);
	u32 sa[5] = {1, 0, 0, 0, 0};		/* SIG_IGN */

	if (argc > 1 && num(argv[1]) > 0 && num(argv[1]) < 65536)
		port = num(argv[1]);
	if (argc > 2)
		bindaddr = ip4(argv[2]);
	if (argc > 3 && argv[3][0] == 'p')
		passive = 1;
	sc(SYS_rt_sigaction, 13, (long)sa, 0, 8, 0, 0);	/* SIGPIPE */
	sd_init(&st);
	update_now();
	d_listen = d_adc = d_ui = d_gps = now;
	hw_open();
	read_inputs();

	for (;;) {
		struct pfd p[3];
		int timeout = 1000, i, n;
		u32 dl[5];

		update_now();
		if (passive)
			d_adc = d_ui = d_gps = now + 1000;
		if (lfd < 0 && due(d_listen))
			listen_open();
		if (gfd < 0 && due(d_gps))
			gps_open();
		if (due(d_adc))
			adc_step();
		if (due(d_ui)) {
			ui_write();
			d_ui = now + UI_MS;
		}
		if (cfd >= 0 && due(d_client))
			client_close();

		dl[0] = d_adc;
		dl[1] = d_ui;
		dl[2] = cfd >= 0 ? d_client : now + 1000;
		dl[3] = lfd < 0 ? d_listen : now + 1000;
		dl[4] = gfd < 0 ? d_gps : now + 1000;
		for (i = 0; i < 5; i++) {
			int t = (int)(dl[i] - now);

			if (t < timeout)
				timeout = t;
		}
		if (timeout < 0)
			timeout = 0;

		p[0].fd = cfd < 0 ? lfd : -1;	/* one request at a time: pending connections wait in the backlog */
		p[1].fd = cfd;
		p[2].fd = gfd;
		for (i = 0; i < 3; i++) {
			p[i].events = POLLIN;
			p[i].revents = 0;
		}
		n = (int)sc(SYS_poll, (long)p, 3, timeout, 0, 0, 0);
		if (n <= 0)
			continue;
		update_now();
		if (p[2].fd >= 0 && (p[2].revents & (POLLIN | POLLERR | POLLHUP)))
			gps_readable();
		if (p[1].fd >= 0 && cfd >= 0 && (p[1].revents & (POLLIN | POLLERR | POLLHUP)))
			client_readable();
		if (p[0].fd >= 0 && cfd < 0 && (p[0].revents & POLLIN))
			accept_one();
	}
}

__asm__(".globl _start\n_start:\n mov r0, sp\n bl main_c\n mov r7, #1\n svc 0\n");

/* No libgcc: shift-and-subtract unsigned division for the compiler's division calls. */
unsigned int __aeabi_uidiv(unsigned int n, unsigned int d)
{
	unsigned int q = 0, r = 0;
	int i;

	if (!d)
		return 0;
	for (i = 31; i >= 0; i--) {
		r = (r << 1) | ((n >> i) & 1);
		if (r >= d) {
			r -= d;
			q |= 1u << i;
		}
	}
	return q;
}

struct uidivmod_res {
	unsigned int q, r;
};

struct uidivmod_res __aeabi_uidivmod(unsigned int n, unsigned int d)
{
	struct uidivmod_res x;

	x.q = __aeabi_uidiv(n, d);
	x.r = n - x.q * d;
	return x;
}
