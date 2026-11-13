/* homtom-snd [HZ [SECONDS [VOL0-127]]]: play a mono sine through the WM8750 codec and the speaker amp, no kernel audio driver.
 * Freestanding (raw EABI syscalls, no libc), ARMv4T, /dev/mem. Style follows tools/devtools/devmem.c.
 * NOT run on the device. Every claim is tagged [V] source-verified or [H] hypothesis.
 *
 * Stock sources (S = .runtime/kernel-build, B = S/drivers/barcelona/sound, M = S/arch/arm/mach-s3c2410):
 *   codec       WM8971 table, chip is WM8750BG (same register map)         [V] M/tomtomgo-iomodena.h:29; wm8971.c:21-66  [H] chip marking from docs/HARDWARE.md
 *   control     3-wire bit-bang, write only, 16-bit word (reg<<9)|data, MSB first, CSB low during word [V] B/spi.c:9-37, wm8971.c:163-167
 *               CSB = L3MODE GPB3, SCLK = L3CLOCK GPB4, SDIN = L3DATA GPB2                                  [V] iomodena.h:79-81
 *   I2S pins    LRCK GPE0, SCLK GPE1, CDCLK GPE2, SDI GPE3, SDO GPE4, all function 2                       [V] iomodena.h:73-78; driver.c:1938-1944
 *   codec power DAC_PWR_ON GPJ15 active high, 50 ms settle                                                 [V] iomodena.h:72; driver.c:1936-1937
 *   speaker amp AMP_ON GPG11 active high, driven low at power-on, high when a stream opens                 [V] iomodena.h:71; driver.c:1934, 1536
 *               OUT2 (speaker, differential) = R26 |= 0x18, volume R40/R41 = 0x180|vol                    [V] wm8971.c:188; driver.c/wm8971.c:199-210
 *   headphone   HEADPHONE_DETECT GPG3, active low (input); jack output is OUT1, volume R2/R3 = 0x180|vol  [V] iomodena.h:104; driver.c:1359; wm8971.c:225-234
 *   MUTE_INT/MUTE_EXT/MIC_SW: not defined in tomtomgo-iomodena.h, the stock calls are no-ops on Modena     [V] grep of iomodena.h
 *   IIS regs    base 0x55000000: IISCON +0, IISMOD +4, IISFIC +8, IISPSR +0xC, IISTXD +0x10               [V] L/sound/soc/samsung/regs-i2s-v2.h:12-17; map-s3c24xx.h:76
 *   stock IISMOD for slave codec: LOCLKR | FMTI2S | 256FS | BFS32FS | XMITRECV | EMASTER (CDCLKCON clear) [V] iis-s3c2443.c:56-72 (regs-iis.h:127-187)
 *   stock IISCON = 7 (IIS active, TX and RX DMA-active bits), IISFIC = 0, IISPSR = PSRAEN                  [V] iis-s3c2443.c:92-100
 *   codec R7 = 0x02 (I2S, 16 bit, slave), R8 = 0x20 (256fs)                                                [V] wm8971.c:25-30, 447, 452
 *
 * Deliberate deviation from stock: the stock slave path retunes EPLL to 32.769/67.714/73.714 MHz (iis-s3c2443.c:125-175).
 * EPLL also clocks HSMMC1 (moviNAND): in 6.1, div_hsmmc1 hangs off esysclk (L/drivers/clk/samsung/clk-s3c2443.c:107, 64) [V].
 * So this tool NEVER touches EPLLCON/CLKSRC bits 8:7 or bit 6. It selects the I2S clock mux CLKSRC[15:14] = 2 (epllref, the 12 MHz
 * reference, [V] clk-s3c2443.c:68,76; that it is 12 MHz is [H] from s3c2443_init_clocks(12000000) in the board file), PSR divider 1:
 *   fs = 12 MHz / 256 = 46875 Hz (2.3 percent flat of 48 kHz) [H]; the codec sees 256fs MCLK on CDCLK (GPE2) [H].
 * If no sound: first suspect the IISMOD/IMS clock-select bits and which pin the codec MCLK really comes from [H].
 *
 * Registers written (masked read-modify-write only):
 *   GPJCON 0x560000D0 mask 0xC0000000 (pin 15 only), GPJDAT 0x560000D4 mask 0x00008000 (bit 15 only)
 *     -> never GPJDAT bit 10 (PWR_RST) / 14 (moviNAND power), never GPJCON pins 10/14 (checked at run time by gj())
 *   GPGCON 0x56000060 masks 0x00C00000 (GPG11) and 0x000000C0 (GPG3 input); GPGDAT 0x56000064 bit 11 only
 *   GPBCON 0x56000010 mask 0x000003F0 (GPB2-4 only, leaves GPB0 backlight and GPB1 BT_MODE); GPBDAT 0x56000014 mask 0x1C
 *   GPECON 0x56000040 mask 0x000003FF (GPE0-4 only; GPE5-10 legacy SDI, GPE11-15 SPI/I2C untouched), GPEDAT 0x56000044 bit 4
 *   no GPLCON/GPLDAT access at all (moviNAND bus)
 *   PCLKCON 0x4C000034 bit 9, SCLKCON 0x4C000038 bit 9, CLKSRC 0x4C000020 bits 15:14 only
 *   IIS 0x55000000.. as above
 *
 * Order (mirrors sound_poweron, driver.c:1930-1949, then iis hw_init, then wm8971_init_registers wm8971.c:175-197):
 *   AMP low -> DAC_PWR high, 50 ms -> clocks + IIS running (MCLK present) -> GPE0-4 function 2 -> codec reset R15 and table
 *   -> R5 = 0 unmute -> volume -> AMP high -> samples -> AMP low -> R5 = 0x08 mute -> DAC_PWR low (stock sound_poweroff, driver.c:1953-1958).
 *
 * Build: clang --target=armv4t-linux-gnueabi -march=armv4t -mfloat-abi=soft -Os -nostdlib -static -ffreestanding -fuse-ld=lld homtom-snd.c -o homtom-snd */
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

static u32 num(const char *s)
{
	u32 v = 0;

	for (; *s >= '0' && *s <= '9'; s++)
		v = v * 10 + (*s - '0');
	return v;
}

static void msleep(u32 ms)
{
	long ts[2] = { ms / 1000, (ms % 1000) * 1000000L };

	sc(162, (long)ts, 0, 0, 0, 0, 0);	/* nanosleep */
}

static volatile u32 *map(long fd, u32 pa)
{
	long m = sc(192, 0, 4096, 3, 1, fd, pa >> 12);	/* mmap2 */

	return (m < 0 && m > -4096) ? 0 : (volatile u32 *)m;
}

static volatile u32 *clk, *iis, *gpio;

static void wr(volatile u32 *p, u32 mask, u32 val)
{
	*p = (*p & ~mask) | (val & mask);
}

/* GPIO write with the repo hazard guard: GPJDAT bits 10/14, GPJCON pins 10/14, any GPL write. */
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

	gw(GPBDAT, 1u << 3, 0);					/* CSB low */
	for (i = 0; i < 16; i++) {
		gw(GPBDAT, 1u << 4, 0);				/* SCLK low */
		gw(GPBDAT, 1u << 2, (w & 0x8000) ? 1u << 2 : 0);	/* SDIN */
		gw(GPBDAT, 1u << 4, 1u << 4);			/* SCLK high: codec samples */
		w <<= 1;
	}
	gw(GPBDAT, 1u << 3, 1u << 3);				/* CSB high: latch */
}

static void codec(u16 reg, u16 val)
{
	l3_send((u16)((reg << 9) | (val & 0x1ff)));
}

/* wm8971.c:21-66 verbatim; 0xffff = NO_INIT. R26 gets |0x18 (OUT2) for the differential amp, wm8971.c:183-188. */
static const u16 wm8971[43] = {
	0x80, 0x80, 0x79, 0x79, 0xffff, 0x08, 0xffff, 0x02, 0x20, 0xffff,
	0xff, 0xff, 0x0f, 0x0f, 0xffff, 0xffff, 0xffff, 0x7b, 0x00, 0x32,
	0x00, 0x1ff, 0x1ff, 0xc0, 0x10, 0xfe, 0x1e4, 0x00, 0xffff, 0xffff,
	0xffff, 0x00, 0x00, 0x00, 0x150, 0x50, 0x50, 0x150, 0x150, 0x150,
	0x79, 0x79, 0x80,
};

static int isin(u32 p)		/* parabolic sine, p = 16-bit phase, +-8192 */
{
	u32 x = p & 0x7fff;
	int y = (int)((x * (32768 - x)) >> 15);

	return (p & 0x8000) ? -y : y;
}

int main_c(long *sp)
{
	int argc = (int)sp[0];
	char **argv = (char **)(sp + 1);
	u32 hz = 440, secs = 2, vol = 100, n, i, step, ph = 0, total, hp;
	long fd;

	if (argc > 1)
		hz = num(argv[1]);
	if (argc > 2)
		secs = num(argv[2]);
	if (argc > 3)
		vol = num(argv[3]);
	if (hz < 50 || hz > 10000 || secs > 30 || vol > 121)	/* 121 = 0 dB, stock default 0x79 */
		return 2;

	fd = sc(5, (long)"/dev/mem", 2 | 0x101000, 0, 0, 0, 0);	/* O_RDWR|O_SYNC */
	if (fd < 0)
		return 1;
	clk = map(fd, 0x4C000000);
	iis = map(fd, 0x55000000);
	gpio = map(fd, 0x56000000);
	if (!clk || !iis || !gpio)
		return 1;

	/* headphone detect GPG3: input, active low */
	gw(GPGCON, 0xC0, 0);
	hp = !((*(gpio + GPGDAT / 4) >> 3) & 1);

	/* AMP_ON GPG11 low (output), then DAC_PWR_ON GPJ15 high (DAT first, then output) */
	gw(GPGDAT, 1u << 11, 0);
	gw(GPGCON, 0x00C00000, 0x00400000);
	gw(GPJDAT, 1u << 15, 1u << 15);
	gw(GPJCON, 0xC0000000, 0x40000000);
	msleep(50);

	/* clocks: PCLKCON bit 9 (I2S0), SCLKCON bit 9 (I2SCLK), I2S mux CLKSRC[15:14] = 2 (epllref) */
	wr(clk + 0x34 / 4, 1u << 9, 1u << 9);
	wr(clk + 0x38 / 4, 1u << 9, 1u << 9);
	wr(clk + 0x20 / 4, 3u << 14, 2u << 14);

	/* IIS: stock slave-codec setup, PSR divider 1 */
	*(iis + 0x00 / 4) = 0;
	*(iis + 0x04 / 4) = (1u << 7) | (0 << 5) | (0 << 3) | (0 << 1) | (2u << 8) | (1u << 10);
	*(iis + 0x08 / 4) = 1u << 15;			/* TX FIFO flush */
	*(iis + 0x08 / 4) = 0;
	*(iis + 0x0c / 4) = 1u << 15;			/* PSRAEN, value 0 = divide by 1 */
	*(iis + 0x00 / 4) = 7;				/* stock IISCON */

	/* I2S pins GPE0-4 = function 2 */
	gw(GPECON, 0x3ff, 0x2aa);
	msleep(50);

	/* codec 3-wire bus: idle CSB high, SCLK high, then GPB2-4 outputs */
	gw(GPBDAT, 0x1c, 0x18);
	gw(GPBCON, 0x3f0, 0x150);
	codec(15, 0);
	for (i = 0; i < 43; i++) {
		u16 r = wm8971[i];

		if (r == 0xffff)
			continue;
		if (i == 26)
			r |= 0x18;
		codec((u16)i, r);
	}
	codec(2, 0x180 | vol);				/* OUT1 (jack) */
	codec(3, 0x180 | vol);
	codec(40, 0x180 | vol);				/* OUT2 (speaker) */
	codec(41, 0x180 | vol);
	codec(5, 0);					/* unmute DAC */

	if (!hp)
		gw(GPGDAT, 1u << 11, 1u << 11);		/* AMP_ON; stays off with a headphone plugged in */

	total = 46875 * secs;
	step = (hz * 65536u) / 46875u;
	for (n = 0; n < total; n++) {
		u32 s = (u32)(s16)isin(ph) & 0xffff;

		while (*(iis + 0x00 / 4) & (1u << 8))	/* TX FIFO full */
			;
		*(iis + 0x10 / 4) = (s << 16) | s;
		ph = (ph + step) & 0xffff;
	}
	msleep(20);

	gw(GPGDAT, 1u << 11, 0);			/* AMP off */
	codec(5, 0x08);					/* DAC mute */
	*(iis + 0x00 / 4) = 0;
	gw(GPEDAT, 1u << 4, 0);				/* SDO low like IIS_IDLE_TX */
	gw(GPECON, 0x300, 0x100);
	gw(GPJDAT, 1u << 15, 0);			/* DAC_PWR_ON off */
	return 0;
}

__asm__(".globl _start\n_start:\n mov r0, sp\n bl main_c\n mov r7, #1\n svc 0\n");
