/* homtom-play: stream a WAV or raw PCM file (or a test tone) to /dev/homtom-audio (kernel/patches/audio/homtom-audio).
 *
 * Usage: homtom-play [options] FILE|-        FILE is a 16-bit PCM WAV (mono/stereo, any rate) unless --raw is given
 *        homtom-play [options] --tone HZ [--secs N]
 *        homtom-play --silent-test SECS      DMA check only: no codec power, no amp, streams zeros, prints the status
 *   --vol 48-117      raw LOUT1/ROUT1/LOUT2/ROUT2 value like homtom-snd2 (default 117 = stock; 121 = 0 dB is refused)
 *   --amp on|off|auto amplifier GPG11 as in homtom-snd2 (auto: off when the headphone detect GPG3 reads low)
 *   --raw RATE CH     input is headerless signed 16-bit little-endian, CH = 1 or 2
 *   --stereo          keep L and R apart (default: mix to mono and send it on both channels, like the stock driver)
 *   --rate HZ         output frame rate the resampler assumes in mode 0 (default 46875 = 12 MHz / 256); use the
 *                     fs_measured value from the status file if it differs
 *   --codec-master 48000|44100   experimental: IIS slave, codec master in USB mode from the 12 MHz MCLK
 *   --bclkinv 0|1     codec R7 bit 7 in --codec-master (default 1 = 0xC2, the stock S3C2443 codec-master value)
 *   --secs N          stop after N seconds (tone default 2)
 *
 * The codec, DAC power and amplifier are driven exactly like tools/devtools/homtom-snd2.c (same table, same order, masked
 * GPIO writes through /dev/mem, never GPJ10/14 or GPL). The IIS block, its clocks, GPE0-4 and the DMA channel belong to
 * the kernel module: opening /dev/homtom-audio starts MCLK and the DMA ring with silence, so the codec is initialised
 * with its clocks running, as homtom-snd2 does. SIGINT/SIGTERM/SIGHUP switch the amplifier off and power down.
 *
 * Build: tools/devtools/build.sh homtom-play */
typedef unsigned int u32;
typedef unsigned short u16;
typedef short s16;

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

#define SYS_EXIT 1
#define SYS_READ 3
#define SYS_WRITE 4
#define SYS_OPEN 5
#define SYS_CLOSE 6
#define SYS_MKNOD 14
#define SYS_FSYNC 118
#define SYS_NANOSLEEP 162
#define SYS_RT_SIGACTION 174
#define SYS_MMAP2 192
#define SYS_CLOCK_GETTIME 263
#define EINTR 4
#define EAGAIN 11

static void *memset(void *d, int c, unsigned long n)
{
	unsigned char *p = d;

	while (n--)
		*p++ = (unsigned char)c;
	return d;
}

static void *memcpy(void *d, const void *s, unsigned long n)
{
	unsigned char *p = d;
	const unsigned char *q = s;

	while (n--)
		*p++ = *q++;
	return d;
}

static u32 udiv(u32 n, u32 d)
{
	u32 q = 0, bit = 1;

	if (!d)
		return 0;
	while (d < n && !(d & 0x80000000u)) {
		d <<= 1;
		bit <<= 1;
	}
	while (bit) {
		if (n >= d) {
			n -= d;
			q |= bit;
		}
		d >>= 1;
		bit >>= 1;
	}
	return q;
}

static u32 slen(const char *s)
{
	u32 n = 0;

	while (s[n])
		n++;
	return n;
}

static void out(const char *s)
{
	sc(SYS_WRITE, 1, (long)s, slen(s), 0, 0, 0);
}

static void dec(u32 v)
{
	char b[12];
	int i = 11;

	b[i] = 0;
	do {
		b[--i] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	out(b + i);
}

static int eq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

static u32 num(const char *s)
{
	u32 v = 0;

	if (!s)
		return 0;
	for (; *s >= '0' && *s <= '9'; s++)
		v = v * 10 + (*s - '0');
	return v;
}

static void msleep(u32 ms)
{
	long ts[2] = { ms / 1000, (ms % 1000) * 1000000L };

	sc(SYS_NANOSLEEP, (long)ts, 0, 0, 0, 0, 0);
}

static u32 now_ms(void)
{
	long ts[2];

	sc(SYS_CLOCK_GETTIME, 1, (long)ts, 0, 0, 0, 0);
	return (u32)ts[0] * 1000u + (u32)ts[1] / 1000000u;
}

static volatile int stop_sig;

static void on_signal(int s)
{
	stop_sig = s;
}

extern void play_restorer(void);
__asm__(".globl play_restorer\nplay_restorer:\n mov r7, #173\n svc 0\n");

static void catch_signals(void)
{
	static const int sigs[3] = { 1, 2, 15 };
	struct {
		void (*handler)(int);
		unsigned long flags;
		void (*restorer)(void);
		unsigned long mask[2];
	} act;
	int i;

	for (i = 0; i < 3; i++) {
		act.handler = on_signal;
		act.flags = 0x04000000;	/* SA_RESTORER, no SA_RESTART: a blocked write returns at once */
		act.restorer = play_restorer;
		act.mask[0] = act.mask[1] = 0;
		sc(SYS_RT_SIGACTION, sigs[i], (long)&act, 0, 8, 0, 0);
	}
}

static volatile u32 *map(long fd, u32 pa)
{
	long m = sc(SYS_MMAP2, 0, 4096, 3, 1, fd, pa >> 12);

	return (m < 0 && m > -4096) ? 0 : (volatile u32 *)m;
}

static volatile u32 *gpio;

static void wr(volatile u32 *p, u32 mask, u32 val)
{
	*p = (*p & ~mask) | (val & mask);
}

static int gw(u32 off, u32 mask, u32 val)
{
	if ((off == 0xD4 && (mask & 0x4400)) || (off == 0xD0 && (mask & 0x30300000)) || (off >= 0xF0 && off <= 0xFC))
		return -1;
	wr(gpio + off / 4, mask, val);
	return 0;
}

#define GPBCON 0x10
#define GPBDAT 0x14
#define GPECON 0x40
#define GPEDAT 0x44
#define GPGCON 0x60
#define GPGDAT 0x64
#define GPJCON 0xD0
#define GPJDAT 0xD4

static void l3_send(u16 w)
{
	int i;

	gw(GPBDAT, 1u << 3, 0);
	for (i = 0; i < 16; i++) {
		gw(GPBDAT, 1u << 4, 0);
		gw(GPBDAT, 1u << 2, (w & 0x8000) ? 1u << 2 : 0);
		gw(GPBDAT, 1u << 4, 1u << 4);
		w <<= 1;
	}
	gw(GPBDAT, 1u << 3, 1u << 3);
}

static void codec(u16 reg, u16 val)
{
	l3_send((u16)((reg << 9) | (val & 0x1ff)));
}

static const u16 wm8971[43] = {
	0x80, 0x80, 0x79, 0x79, 0xffff, 0x08, 0xffff, 0x02, 0x20, 0xffff,
	0xff, 0xff, 0x0f, 0x0f, 0xffff, 0xffff, 0xffff, 0x7b, 0x00, 0x32,
	0x00, 0x1ff, 0x1ff, 0xc0, 0x10, 0xfe, 0x1e4, 0x00, 0xffff, 0xffff,
	0xffff, 0x00, 0x00, 0x00, 0x150, 0x50, 0x50, 0x150, 0x150, 0x150,
	0x79, 0x79, 0x80,
};

static void power_up(void)
{
	gw(GPGDAT, 1u << 11, 0);
	gw(GPGCON, 0x00C00000, 0x00400000);
	gw(GPGCON, 0xC0, 0);
	gw(GPJDAT, 1u << 15, 1u << 15);
	gw(GPJCON, 0xC0000000, 0x40000000);
	msleep(50);
}

/* homtom-snd2 power_down() without the IISCON write: closing /dev/homtom-audio stops the IIS and the DMA. */
static void amp_off_mute(void)
{
	gw(GPGDAT, 1u << 11, 0);
	codec(5, 0x08);
}

static void power_down_rest(void)
{
	gw(GPEDAT, 1u << 4, 0);
	gw(GPECON, 0x300, 0x100);
	gw(GPJDAT, 1u << 15, 0);
}

static void codec_init(u32 vol)
{
	u32 i;

	gw(GPBDAT, 0x1c, 0x18);
	gw(GPBCON, 0x3f0, 0x150);
	codec(15, 0);
	msleep(5);
	for (i = 0; i < 43; i++) {
		u16 r = wm8971[i];

		if (r == 0xffff)
			continue;
		if (i == 26)
			r |= 0x18;
		codec((u16)i, r);
	}
	codec(2, 0x80 | vol);
	codec(3, 0x180 | vol);
	codec(40, 0x80 | vol);
	codec(41, 0x180 | vol);
	codec(5, 0);
}

static int isin(u32 p)
{
	u32 x = p & 0x7fff;
	int y = (int)((x * (32768 - x)) >> 15);

	return (p & 0x8000) ? -y : y;
}

static const char dev_path[] = "/dev/homtom-audio";
static const char status_path[] = "/sys/class/misc/homtom-audio/status";

static long open_dev(void)
{
	char b[16];
	long fd = sc(SYS_OPEN, (long)dev_path, 1, 0, 0, 0, 0), n;
	u32 maj, min, i;

	if (fd != -2)
		return fd;
	/* no devtmpfs node: create it from /sys/class/misc/homtom-audio/dev ("10:N") */
	fd = sc(SYS_OPEN, (long)"/sys/class/misc/homtom-audio/dev", 0, 0, 0, 0, 0);
	if (fd < 0)
		return fd;
	n = sc(SYS_READ, fd, (long)b, sizeof(b) - 1, 0, 0, 0);
	sc(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
	if (n <= 0)
		return -2;
	b[n] = 0;
	maj = num(b);
	for (i = 0; b[i] && b[i] != ':'; i++)
		;
	min = b[i] ? num(b + i + 1) : 0;
	sc(SYS_MKNOD, (long)dev_path, 0020000 | 0600, (long)((maj << 8) | (min & 0xff) | ((min & ~0xffu) << 12)), 0, 0, 0);
	return sc(SYS_OPEN, (long)dev_path, 1, 0, 0, 0, 0);
}

static int set_mode(u32 m)
{
	long fd = sc(SYS_OPEN, (long)"/sys/module/homtom_audio/parameters/mode", 1, 0, 0, 0, 0);
	char c = (char)('0' + m);

	if (fd < 0)
		return (int)fd;
	sc(SYS_WRITE, fd, (long)&c, 1, 0, 0, 0);
	sc(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
	return 0;
}

static void print_status(void)
{
	static char b[1024];
	long fd = sc(SYS_OPEN, (long)status_path, 0, 0, 0, 0, 0), n;

	if (fd < 0)
		return;
	n = sc(SYS_READ, fd, (long)b, sizeof(b) - 1, 0, 0, 0);
	sc(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
	if (n > 0) {
		b[n] = 0;
		out(b);
	}
}

/* input reader: buffered, works on pipes (no seeks) */
static long in_fd;
static unsigned char ibuf[8192];
static u32 ipos, ilen;
static int ieof;

static u32 rd(void *dst, u32 n)
{
	unsigned char *d = dst;
	u32 got = 0;

	while (got < n) {
		if (ipos == ilen) {
			long r;

			if (ieof)
				break;
			r = sc(SYS_READ, in_fd, (long)ibuf, sizeof(ibuf), 0, 0, 0);
			if (r == -EINTR && !stop_sig)
				continue;
			if (r <= 0) {
				ieof = 1;
				break;
			}
			ipos = 0;
			ilen = (u32)r;
		}
		d[got++] = ibuf[ipos++];
	}
	return got;
}

static u32 le32(const unsigned char *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24);
}

static u32 in_rate, in_ch;

static int parse_wav(void)
{
	unsigned char h[16];
	u32 len, fmt_ok = 0;

	if (rd(h, 12) != 12 || h[0] != 'R' || h[1] != 'I' || h[2] != 'F' || h[3] != 'F' || h[8] != 'W' || h[11] != 'E')
		return -1;
	for (;;) {
		if (rd(h, 8) != 8)
			return -1;
		len = le32(h + 4);
		if (h[0] == 'f' && h[1] == 'm' && h[2] == 't' && len >= 16) {
			u32 tag, bits;

			if (rd(h, 16) != 16)
				return -1;
			tag = h[0] | (h[1] << 8);
			in_ch = h[2] | (h[3] << 8);
			in_rate = le32(h + 4);
			bits = h[14] | (h[15] << 8);
			if ((tag != 1 && tag != 0xfffe) || bits != 16)
				return -2;
			fmt_ok = 1;
			len -= 16;
		} else if (h[0] == 'd' && h[1] == 'a' && h[2] == 't' && h[3] == 'a') {
			return fmt_ok ? 0 : -1;
		}
		len += len & 1;
		while (len) {
			u32 k = len > 16 ? 16 : len;

			if (rd(h, k) != k)
				return -1;
			len -= k;
		}
	}
}

static int next_frame(int *l, int *r)
{
	unsigned char b[4];

	if (rd(b, in_ch * 2) != in_ch * 2)
		return 0;
	*l = (s16)(b[0] | (b[1] << 8));
	*r = in_ch == 2 ? (s16)(b[2] | (b[3] << 8)) : *l;
	return 1;
}

static u32 obuf[1024];
static u32 on, frames_out;

static int flush(long fd)
{
	unsigned char *p = (unsigned char *)obuf;
	u32 left = on * 4;

	while (left && !stop_sig) {
		long w = sc(SYS_WRITE, fd, (long)p, left, 0, 0, 0);

		if (w == -EINTR || w == -EAGAIN)
			continue;
		if (w <= 0) {
			out("write to /dev/homtom-audio failed\n");
			return -1;
		}
		p += w;
		left -= (u32)w;
	}
	frames_out += on;
	on = 0;
	return stop_sig ? -1 : 0;
}

static int put(long fd, int l, int r, u32 stereo)
{
	if (!stereo)
		l = r = (l + r) >> 1;
	obuf[on++] = ((u32)(l & 0xffff)) | ((u32)(r & 0xffff) << 16);
	return on == 1024 ? flush(fd) : 0;
}

int main_c(long *sp)
{
	int argc = (int)sp[0];
	char **argv = (char **)(sp + 1);
	u32 vol = 117, amp = 1, stereo = 0, rate = 46875, cm = 0, bclkinv = 1, secs = 0, tone = 0, silent = 0, raw = 0;
	u32 i, hp, max_frames, t0;
	const char *file = 0;
	long memfd, fd;
	int rc = 0;

	for (i = 1; i < (u32)argc; i++) {
		const char *a = argv[i], *v = (i + 1 < (u32)argc) ? argv[i + 1] : 0;

		if (eq(a, "--vol"))
			vol = num(v), i++;
		else if (eq(a, "--amp")) {
			amp = (v && eq(v, "off")) ? 0 : (v && eq(v, "auto")) ? 2 : 1;
			i++;
		} else if (eq(a, "--raw") && i + 2 < (u32)argc) {
			raw = 1;
			in_rate = num(v);
			in_ch = num(argv[i + 2]);
			i += 2;
		} else if (eq(a, "--stereo"))
			stereo = 1;
		else if (eq(a, "--rate"))
			rate = num(v), i++;
		else if (eq(a, "--codec-master"))
			cm = num(v), i++;
		else if (eq(a, "--bclkinv"))
			bclkinv = num(v), i++;
		else if (eq(a, "--secs"))
			secs = num(v), i++;
		else if (eq(a, "--tone"))
			tone = num(v), i++;
		else if (eq(a, "--silent-test"))
			silent = num(v), i++;
		else if (a[0] != '-' || eq(a, "-"))
			file = a;
		else {
			out("unknown argument: ");
			out(a);
			out("\n");
			return 2;
		}
	}
	if (cm)
		rate = cm == 48000 ? 48000 : 44118;
	if (vol < 48 || vol > 117 || (cm && cm != 48000 && cm != 44100) || rate < 8000 || rate > 65535 || secs > 3600 ||
	    (tone && (tone < 50 || tone > 10000)) || silent > 60 || (!tone && !silent && !file)) {
		out("bad arguments (vol 48-117, codec-master 48000|44100, tone 50-10000 Hz, FILE or --tone)\n");
		return 2;
	}
	if (raw && (in_rate < 4000 || in_rate > 192000 || in_ch < 1 || in_ch > 2))
		return 2;
	if (tone && !secs)
		secs = 2;
	catch_signals();

	if (set_mode(cm ? 1 : 0)) {
		out("homtom-audio module not loaded (insmod homtom-audio.ko)\n");
		return 1;
	}

	if (silent) {
		fd = open_dev();
		if (fd < 0) {
			out("cannot open /dev/homtom-audio\n");
			return 1;
		}
		t0 = now_ms();
		while (!stop_sig && now_ms() - t0 < silent * 1000) {
			while (on < 1024)
				obuf[on++] = 0;
			if (flush(fd))
				break;
		}
		print_status();
		sc(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
		return 0;
	}

	if (file) {
		in_fd = eq(file, "-") ? 0 : sc(SYS_OPEN, (long)file, 0, 0, 0, 0, 0);
		if (in_fd < 0) {
			out("cannot open input\n");
			return 1;
		}
		if (!raw) {
			int w = parse_wav();

			if (w) {
				out(w == -2 ? "only 16-bit PCM WAV is supported\n" : "not a WAV file (use --raw RATE CH)\n");
				return 1;
			}
			if (in_ch < 1 || in_ch > 2 || in_rate < 4000 || in_rate > 192000) {
				out("WAV must be mono or stereo, 4000-192000 Hz\n");
				return 1;
			}
		}
	}

	memfd = sc(SYS_OPEN, (long)"/dev/mem", 2 | 0x101000, 0, 0, 0, 0);
	if (memfd < 0)
		return 1;
	gpio = map(memfd, 0x56000000);
	if (!gpio)
		return 1;

	power_up();
	hp = !((gpio[GPGDAT / 4] >> 3) & 1);
	fd = open_dev();
	if (fd < 0) {
		out("cannot open /dev/homtom-audio (busy, or module missing)\n");
		gw(GPJDAT, 1u << 15, 0);
		return 1;
	}
	msleep(20);
	codec_init(vol);
	if (cm) {
		codec(7, bclkinv ? 0xC2 : 0x42);
		codec(8, cm == 48000 ? 0x01 : 0x23);
	}
	if (amp == 1 || (amp == 2 && !hp))
		gw(GPGDAT, 1u << 11, 1u << 11);
	out(hp ? "headphone plugged (GPG3 low)" : "headphone not plugged");
	out(", output ");
	dec(rate);
	out(" Hz\n");

	max_frames = secs ? secs * rate : 0xffffffffu;
	t0 = now_ms();
	if (tone) {
		u32 ph = 0, step = udiv(tone * 65536u, rate), n;

		for (n = 0; n < max_frames && !stop_sig; n++) {
			int s = isin(ph);

			ph = (ph + step) & 0xffff;
			if (put(fd, s, s, 1))
				break;
		}
	} else {
		u32 q = udiv(in_rate, rate), step = (q << 16) | udiv((in_rate - q * rate) << 16, rate), frac = 0, n;
		int al = 0, ar = 0, bl = 0, br = 0, live;

		live = next_frame(&al, &ar);
		if (live && !next_frame(&bl, &br))
			bl = al, br = ar;
		for (n = 0; live && n < max_frames && !stop_sig; n++) {
			u32 f = frac >> 1;
			int l = al + (((bl - al) * (int)f) >> 15), r = ar + (((br - ar) * (int)f) >> 15);

			if (put(fd, l, r, stereo))
				break;
			frac += step;
			while (frac >= 65536 && live) {
				frac -= 65536;
				al = bl;
				ar = br;
				if (!next_frame(&bl, &br)) {
					live = 0;
					break;
				}
			}
		}
	}
	if (!stop_sig && on)
		flush(fd);
	if (!stop_sig)
		sc(SYS_FSYNC, fd, 0, 0, 0, 0, 0);
	amp_off_mute();
	out("frames=");
	dec(frames_out);
	out(" elapsed_ms=");
	dec(now_ms() - t0);
	out(stop_sig ? " (stopped by signal)\n" : "\n");
	print_status();
	sc(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
	power_down_rest();
	if (stop_sig)
		rc = 130;
	return rc;
}

__asm__(".globl _start\n_start:\n mov r0, sp\n bl main_c\n mov r7, #1\n svc 0\n");
