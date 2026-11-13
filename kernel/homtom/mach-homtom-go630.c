// SPDX-License-Identifier: GPL-2.0
/*
 * TomTom GO 630 / Modena (S3C2443) — homtom OS board file for Linux 6.1.y
 *
 * ATAG machine type 605 (MACH_TOMTOMGO). Lab OpenTom claims s3c2410-lcd at
 * 0x4D000000; the SoC also has a TFT block at 0x4C800000. Early bring-up
 * marks BOTH with hardware solid-color overlays (TPAL / WIN0MAP) and paints
 * any scanout buffers we can resolve. Lab link: HSUDC + g_ether. Internal
 * moviNAND on HSMMC1.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/serial_s3c.h>
#include <linux/io.h>
#include <linux/platform_data/mmc-sdhci-s3c.h>
#include <linux/platform_data/usb-s3c2410_udc.h>
#include <linux/platform_data/s3c-hsudc.h>
#include <linux/memblock.h>
#include <linux/pm.h>
#include <linux/reboot.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <asm/fiq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach-types.h>
#include <asm/tlbflush.h>

#include "hardware-s3c24xx.h"
#include "map-s3c24xx.h"
#include "regs-gpio.h"
#include "regs-gpio-s3c24xx.h"
#include "regs-s3c2443-clock.h"
#include "gpio-samsung.h"
#include "gpio-cfg.h"
#include "devs.h"
#include "cpu.h"
#include "s3c24xx.h"
#include "sdhci.h"

#define HOMTOM_TFT_PA		0x4C800000UL
#define HOMTOM_LCD_PA		0x4D000000UL
#define HOMTOM_CLKPWR_PA	0x4C000000UL

/*
 * Static VA for LCD marks inside map_io. Early head.S identity maps at
 * PA==VA are destroyed by prepare_page_table() before mdesc->map_io().
 * Mainline never gives LCD a permanent VA; drivers ioremap. We need one
 * before ioremap exists.
 */
#define HOMTOM_VA_LCD		S3C_ADDR_CPU(0x01000000)

#define HOMTOM_VIDCON0		0x00
#define HOMTOM_VIDW00ADD0B0	0x64
#define HOMTOM_VIDW00ADD0B1	0x68
#define HOMTOM_WIN0MAP		0xD0
#define HOMTOM_WIN1MAP		0xD4

#define HOMTOM_LCDCON1		0x00
#define HOMTOM_LCDSADDR1	0x14
#define HOMTOM_TPAL		0x50

#define HOMTOM_HCLKCON		0x30
#define HOMTOM_HCLKCON_LCDC	(1u << 9)

#define HOMTOM_FB_PIXELS	(480 * 272)
#define HOMTOM_MISCCR_TFT	(1u << 28)

/* Board-side color ladder (see COLOR_LADDER.txt). Stub uses 1–6. */
#define HOMTOM_COL_RED_ENTER	0xFF0000	/* 7  map_io after LCD mapped */
#define HOMTOM_COL_TURQUOISE	0x40E0D0	/* 7a before init_io */
#define HOMTOM_COL_YELLOW	0xFFFF00	/* 7b after init_io */
#define HOMTOM_COL_MAPIO_DONE	0x00FF80	/* 7c map_io return */
#define HOMTOM_COL_PINK		0xFF80C0	/* 8  pre_clk */
#define HOMTOM_COL_LIME		0x80FF00	/* 9  post_clk */
#define HOMTOM_COL_TEAL		0x008080	/* 10 machine_init */
#define HOMTOM_COL_ORANGE	0xFF8000	/* 11 post_devices */
#define HOMTOM_COL_WHITE	0xFFFFFF	/* 12 late */

static struct map_desc homtom_lcd_iodesc[] __initdata = {
	{
		.virtual	= (unsigned long)HOMTOM_VA_LCD,
		.pfn		= __phys_to_pfn(HOMTOM_LCD_PA),
		.length		= S3C24XX_SZ_LCD,
		.type		= MT_DEVICE,
	},
};

static inline u16 homtom_rgb565(u32 rgb24)
{
	u32 r = (rgb24 >> 16) & 0xff;
	u32 g = (rgb24 >> 8) & 0xff;
	u32 b = rgb24 & 0xff;

	return (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static struct s3c2410_uartcfg homtom_uartcfgs[] __initdata = {
	[0] = { .hwport = 0, .flags = 0, .ucon = 0x3c5, .ulcon = 0x03, .ufcon = 0x51 },
	[1] = { .hwport = 1, .flags = 0, .ucon = 0x3c5, .ulcon = 0x03, .ufcon = 0x51 },
	[2] = { .hwport = 2, .flags = 0, .ucon = 0x3c5, .ulcon = 0x03, .ufcon = 0x51 },
};

/* moviNAND is on port L (GPL0-7 DAT, GPL8 CMD, GPL9 CLK, function 2), NOT port E (that is the legacy SDI slot). Verified from the stock 2.6.13 tomtomgo-type.c. */
static void homtom_hsmmc_cfg_gpio(struct platform_device *dev, int width)
{
	s3c_gpio_cfgrange_nopull(S3C2410_GPL(0), width, S3C_GPIO_SFN(2));
	s3c_gpio_cfgrange_nopull(S3C2410_GPL(8), 2, S3C_GPIO_SFN(2));
}

static struct s3c_sdhci_platdata homtom_hsmmc1_pdata __initdata = {
	.cfg_gpio	= homtom_hsmmc_cfg_gpio,
	.max_width	= 4,
	.cd_type	= S3C_SDHCI_CD_PERMANENT,
};

static void homtom_lcd_gpio(void)
{
	gpio_request_one(S3C2410_GPB(0), GPIOF_OUT_INIT_HIGH, "bl-pwm");
}

static void homtom_hsudc_gpio_init(void)
{
	s3c2410_modify_misccr(S3C2410_MISCCR_USBHOST |
			      S3C2410_MISCCR_USBSUSPND0 |
			      S3C2410_MISCCR_USBSUSPND1, 0);
	gpio_request_one(S3C2410_GPG(8), GPIOF_OUT_INIT_HIGH, "usb-phy-pwr");
	gpio_request_one(S3C2410_GPG(5), GPIOF_OUT_INIT_HIGH, "usb-pwr-bypass");
	mdelay(1);
}

static void homtom_hsudc_gpio_uninit(void)
{
	gpio_set_value(S3C2410_GPG(8), 0);
}


/*
 * Legacy 2.6.13 s3c-udc-hs sequence (hsudc-v2+): bus clock on, PHY power,
 * PHY then function reset, device-mode PHYCTRL, pull-up last.
 */
static void homtom_hsudc_phy_init(void)
{
	void __iomem *c = ioremap(HOMTOM_CLKPWR_PA, SZ_4K);
	u32 v;

	if (!c)
		return;
	writel(readl(c + 0x30) | (1u << 12), c + 0x30);	/* HCLKCON USBDEV */
	writel(readl(c + 0x60) | (1u << 4), c + 0x60);	/* PWRCFG phy power */
	writel(1, c + 0x88);				/* URSTCON phy reset */
	udelay(20);
	writel(4, c + 0x88);				/* function reset */
	udelay(20);
	writel(0, c + 0x88);
	v = readl(c + 0x80);				/* PHYCTRL */
	v &= ~((1u << 0) | (1u << 2));			/* device, no ext clk */
	writel(v, c + 0x80);
	writel(1u << 31, c + 0x84);			/* PHYPWR */
	writel((1u << 4) | (1u << 2), c + 0x8c);	/* UCLKCON func clk */
	udelay(50);
	writel((1u << 31) | (1u << 4) | (1u << 2), c + 0x8c);	/* D+ pull-up */
	iounmap(c);
}

static void homtom_hsudc_phy_uninit(void)
{
	void __iomem *c = ioremap(HOMTOM_CLKPWR_PA, SZ_4K);

	if (!c)
		return;
	writel(readl(c + 0x8c) & ~(1u << 31), c + 0x8c);
	writel(readl(c + 0x60) & ~(1u << 4), c + 0x60);
	iounmap(c);
}

static struct s3c24xx_hsudc_platdata homtom_hsudc_platdata = {
	.epnum		= 9,
	.gpio_init	= homtom_hsudc_gpio_init,
	.gpio_uninit	= homtom_hsudc_gpio_uninit,
	.phy_init	= homtom_hsudc_phy_init,
	.phy_uninit	= homtom_hsudc_phy_uninit,
};

static struct platform_device *homtom_devices[] __initdata = {
	&s3c_device_wdt,
	&s3c_device_hsmmc1,
	&s3c_device_usb_hsudc,
	&s3c2443_device_dma,
};

static void homtom_paint_phys(unsigned long phys, u16 color)
{
	void __iomem *fb;
	unsigned i;

	if (phys < 0x30000000UL || phys >= 0x34000000UL)
		return;
	fb = ioremap(phys, HOMTOM_FB_PIXELS * 2);
	if (!fb)
		return;
	for (i = 0; i < HOMTOM_FB_PIXELS; i++)
		writew(color, fb + i * 2);
	iounmap(fb);
}

static void homtom_enable_lcd_hclk(void)
{
	void __iomem *clk = ioremap(HOMTOM_CLKPWR_PA, SZ_4K);
	u32 h;

	if (!clk)
		return;
	h = readl(clk + HOMTOM_HCLKCON);
	writel(h | HOMTOM_HCLKCON_LCDC, clk + HOMTOM_HCLKCON);
	iounmap(clk);
}

/*
 * TPAL via HOMTOM_VA_LCD (mapped in map_io). Do not poke PA 0x4D000000
 * after prepare_page_table() — that idmap is gone. Skip TFT WIN*: bus hang
 * risk, and a stale WIN overlay would mask TPAL changes.
 */
/* Boot colour ladder (debug): build with -DHOMTOM_COLOR_LADDER; off by default so boot shows no flashing colours and skips the delay loops. */
static void homtom_mark_tpal(u32 rgb24)
{
#ifdef HOMTOM_COLOR_LADDER
	__raw_writel((1u << 24) | rgb24, HOMTOM_VA_LCD + HOMTOM_TPAL);
#endif
}

static void homtom_clear_tpal(void)
{
	__raw_writel(0, HOMTOM_VA_LCD + HOMTOM_TPAL);
}

static void homtom_mark_pause(void)
{
#ifdef HOMTOM_COLOR_LADDER
	volatile unsigned n = 0x02000000;

	while (n--)
		;
#endif
}

static void homtom_display_mark(const char *when, u32 rgb24)
{
	/* disabled: the TPAL overlay (esp. 12_late = white) would mask the LCD text console */
	if (1)
		return;
	void __iomem *lcd = ioremap(HOMTOM_LCD_PA, SZ_4K);
	u32 saddr1, lcdcon1;
	u16 rgb565 = homtom_rgb565(rgb24);

	homtom_enable_lcd_hclk();

	if (lcd) {
		lcdcon1 = readl(lcd + HOMTOM_LCDCON1);
		saddr1 = readl(lcd + HOMTOM_LCDSADDR1);
		writel((1u << 24) | rgb24, lcd + HOMTOM_TPAL);
		homtom_paint_phys(((unsigned long)saddr1) << 1, rgb565);
		pr_info("homtom: %s color=#%06x LCDCON1=0x%08x SADDR1=0x%08x\n",
			when, rgb24, lcdcon1, saddr1);
		iounmap(lcd);
	}
}

static void __init homtom_map_io(void)
{
	/*
	 * paging_init → prepare_page_table cleared PA==VA LCD idmap, then
	 * devicemaps_init calls us. Remap LCD before any TPAL write.
	 *
	 * Clear TPAL first: sky→lime/blank = map_io reached + MMIO works.
	 * Stuck on sky = never reached here (or iotable_init faulted).
	 */
	iotable_init(homtom_lcd_iodesc, ARRAY_SIZE(homtom_lcd_iodesc));
	local_flush_tlb_all();
	homtom_clear_tpal();
	homtom_mark_pause();
	homtom_mark_tpal(HOMTOM_COL_RED_ENTER);
	homtom_mark_pause();
	homtom_mark_tpal(HOMTOM_COL_TURQUOISE);
	homtom_mark_pause();
	s3c24xx_init_io(NULL, 0);
	/* Bootloader may have armed the watchdog (reset ~10 s after start); nothing services it until s3c2410_wdt loads. */
	__raw_writel(0, (void __iomem *)0xF6400000UL);
	/* INFORM0 = 5 as early as the system registers are mapped: a watchdog reset of an image that hangs before init.sh must
	 * still count as "Reboot" for TTBL (otherwise it powers down and needs the power key). */
	__raw_writel(5, (void __iomem *)0xF6100000UL + 0x70);
	homtom_mark_tpal(HOMTOM_COL_YELLOW);
	homtom_mark_pause();
	s3c24xx_init_uarts(homtom_uartcfgs, ARRAY_SIZE(homtom_uartcfgs));
	s3c24xx_set_timer_source(S3C24XX_PWM3, S3C24XX_PWM4);
	homtom_mark_tpal(HOMTOM_COL_MAPIO_DONE);
}

static void __init homtom_init_time(void)
{
	homtom_display_mark("8_pre_clk", HOMTOM_COL_PINK);
	s3c2443_init_clocks(12000000);
	s3c24xx_timer_init();
	homtom_display_mark("9_post_clk", HOMTOM_COL_LIME);
}

/*
 * The bootloader framebuffer is at phys 0x30400000, inside the decompressed 9 MB kernel image
 * (PCs of every crash sat right past 0xC043FC00 = FB end). Point the LCD DMA at a reserved buffer
 * at the top of RAM before anything draws.
 */
#define HOMTOM_FB_PHYS		0x33F00000UL
#define HOMTOM_FB_BYTES		0x40000UL

static void __init homtom_reserve(void)
{
	void __iomem *lcd = (void __iomem *)HOMTOM_LCD_PA;
	u32 s1 = __raw_readl(lcd + HOMTOM_LCDSADDR1);
	u32 s2 = __raw_readl(lcd + 0x18);
	u32 span = (s2 - s1) & 0x1fffff;
	u32 n1 = HOMTOM_FB_PHYS >> 1;

	memblock_reserve(HOMTOM_FB_PHYS, HOMTOM_FB_BYTES);
	__raw_writel(n1, lcd + HOMTOM_LCDSADDR1);
	__raw_writel((n1 + span) & 0x1fffff, lcd + 0x18);
}

/*
 * Reboot/poweroff (the stock kernel did this in arch_reset()/gpio_suicide()).
 * SWRST lives in the statically mapped clock block (S3C_VA_SYS = 0xF6100000, +0x44);
 * watchdog reset is the backup. Poweroff drives PWR_RST (GPJ10, active high on this board revision)
 * like gpio_suicide(); the PMIC cannot cut power on USB bus power, so fall back to a reboot.
 * Userspace: use "reboot -f" / "poweroff -f" (BusyBox otherwise signals a non-BusyBox PID 1).
 */
/* /sys/kernel/homtom_reboot_mode: 0 SWRST then WDT, 1 WDT only, 2 SWRST only, 3 PWR_RST low + WDT */
static int homtom_reboot_mode;

static void homtom_restart(enum reboot_mode mode, const char *cmd)
{
	void __iomem *sys = (void __iomem *)0xF6100000UL;
	void __iomem *wdt = (void __iomem *)0xF6400000UL;

	local_irq_disable();
	/*
	 * TTBL decides from INFORM0 (0x4C000070) after reset: 5 (watchdog) / 9 (software reset) = "Reboot", boots straight on;
	 * anything else = "Power down", where it sleeps unless the power key is held. Verified by writing 5 then SWRST by hand.
	 */
	__raw_writel(homtom_reboot_mode == 2 ? 9 : 5, sys + 0x70);
	if (homtom_reboot_mode != 1) {
		__raw_writel(0x533c2443, sys + 0x44);	/* S3C2443_SWRST */
		mdelay(1000);
	}
	__raw_writel(0, wdt);			/* WTCON off */
	__raw_writel(0x100, wdt + 0x08);	/* WTCNT */
	__raw_writel(0x100, wdt + 0x04);	/* WTDAT */
	__raw_writel((1 << 5) | (1 << 0) | (1 << 3) | (0x80 << 8), wdt);	/* enable, reset, DIV16, prescale */
	mdelay(5000);
}

static void homtom_power_off(void)
{
	local_irq_disable();
	if (!gpio_request_one(S3C2410_GPJ(10), GPIOF_OUT_INIT_HIGH, "pwr-rst"))
		mdelay(3000);
	homtom_restart(REBOOT_HARD, NULL);
}

/*
 * Tick-independent PC sampler (see homtom-fiq.S): WDT as 10 Hz interval timer, routed to FIQ. FIQs are masked in the
 * kernel (F set) and enabled in user mode, so this reports where the CPU is stuck after the user-mode transition.
 */
extern unsigned char homtom_fiq_start[], homtom_fiq_end[];

void homtom_fiq_arm(void)
{
	void __iomem *irq = (void __iomem *)0xF6000000UL;
	void __iomem *wdt = (void __iomem *)0xF6400000UL;

	set_fiq_handler(homtom_fiq_start, homtom_fiq_end - homtom_fiq_start);
	__raw_writel(0, wdt);
	__raw_writel(200, wdt + 0x04);		/* WTDAT */
	__raw_writel(200, wdt + 0x08);		/* WTCNT */
	__raw_writel(__raw_readl(irq + 0x1c) & ~(1u << 13), irq + 0x1c);	/* INTSUBMSK: unmask INT_WDT */
	__raw_writel(__raw_readl(irq + 0x08) & ~(1u << 9), irq + 0x08);	/* INTMSK: unmask WDT_AC97 */
	__raw_writel(__raw_readl(irq + 0x04) | (1u << 9), irq + 0x04);	/* INTMOD: WDT_AC97 -> FIQ */
	__raw_writel((0xFF << 8) | (3 << 3) | (1 << 5) | (1 << 2), wdt);	/* prescale 255, /128, enable, interrupt (no reset) */
	pr_info("homtom: fiq sampler armed\n");
}

/*
 * /sys/kernel/homtom_usb_pullup: 0 drops the USB D+ pull-up (UCLKCON bit 31), 1 raises it again. Same effect as unplugging and
 * replugging the cable (host sees disconnect + bus reset) WITHOUT stopping/starting the UDC (soft_connect does that and the
 * s3c-hsudc start path does not recover on this board).
 */
static ssize_t homtom_usb_pullup_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t n)
{
	void __iomem *uclk = (void __iomem *)0xF610008CUL;	/* S3C_VA_SYS + UCLKCON */
	u32 v = __raw_readl(uclk);

	if (n && buf[0] == '0')
		v &= ~(1u << 31);
	else
		v |= (1u << 31);
	__raw_writel(v, uclk);
	return n;
}
extern void homtom_fbtext_splash(bool on);

static ssize_t homtom_reboot_mode_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t n)
{
	if (n && buf[0] >= '0' && buf[0] <= '2')
		homtom_reboot_mode = buf[0] - '0';
	return n;
}
static struct kobj_attribute homtom_reboot_mode_attr = __ATTR(homtom_reboot_mode, 0200, NULL, homtom_reboot_mode_store);

static ssize_t homtom_splash_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t n)
{
	homtom_fbtext_splash(n && buf[0] != '0');
	return n;
}
static struct kobj_attribute homtom_splash_attr = __ATTR(homtom_splash, 0200, NULL, homtom_splash_store);

static struct kobj_attribute homtom_usb_pullup_attr = __ATTR(homtom_usb_pullup, 0200, NULL, homtom_usb_pullup_store);

static int __init homtom_usb_pullup_init(void)
{
	sysfs_create_file(kernel_kobj, &homtom_splash_attr.attr);
	sysfs_create_file(kernel_kobj, &homtom_reboot_mode_attr.attr);
	return sysfs_create_file(kernel_kobj, &homtom_usb_pullup_attr.attr);
}
late_initcall(homtom_usb_pullup_init);

/*
 * Power key: stock ON_OFF = GPF0, inverted (pressed = low). Hold 2 s => kernel_power_off()
 * (GPJ10 on battery; reboots on USB bus power). Arms only after the key has been seen released, so the
 * press that switched the device on cannot immediately switch it off again.
 */
static struct delayed_work homtom_pwrkey_work;
static unsigned int homtom_pwrkey_held;
static bool homtom_pwrkey_armed;

static void homtom_pwrkey_poll(struct work_struct *w)
{
	static unsigned int polls;

	/* liveness beacon on the LCD console: 1/s for the first 30 s, then every 10 s */
	if (++polls % 10 == 0 && (polls < 300 || polls % 100 == 0))
		pr_info("homtom: alive jiffies %lu\n", jiffies);
	if (gpio_get_value(S3C2410_GPF(0))) {
		homtom_pwrkey_armed = true;
		homtom_pwrkey_held = 0;
	} else if (homtom_pwrkey_armed && ++homtom_pwrkey_held >= 20) {
		pr_emerg("homtom: power key held 2 s, powering off\n");
		kernel_power_off();
	}
	schedule_delayed_work(&homtom_pwrkey_work, HZ / 10);
}

static int __init homtom_pwrkey_init(void)
{
	if (gpio_request_one(S3C2410_GPF(0), GPIOF_IN, "power-key"))
		return 0;
	pr_info("homtom: power key GPF0 idle level=%d\n", gpio_get_value(S3C2410_GPF(0)));
	INIT_DELAYED_WORK(&homtom_pwrkey_work, homtom_pwrkey_poll);
	schedule_delayed_work(&homtom_pwrkey_work, HZ);
	return 0;
}
late_initcall(homtom_pwrkey_init);

/*
 * The bootloader leaves interrupt sources unmasked/pending (INTMSK was 0x6FFB5CBF when the machine froze ~10 s
 * after boot with no tick). Mainline expects everything masked at boot; drivers unmask via request_irq.
 */
static void __init homtom_init_irq(void)
{
	void __iomem *irq = (void __iomem *)0xF6000000UL;	/* S3C24XX_VA_IRQ */

	s3c2443_init_irq();
	__raw_writel(0, irq + 0x04);		/* INTMOD: every source to IRQ, none to FIQ (user mode runs with FIQ unmasked) */
	__raw_writel(0xffffffff, irq + 0x08);	/* INTMSK */
	__raw_writel(0xffffffff, irq + 0x1c);	/* INTSUBMSK */
	__raw_writel(0xffffffff, irq + 0x00);	/* SRCPND */
	__raw_writel(0xffffffff, irq + 0x10);	/* INTPND */
	__raw_writel(0xffffffff, irq + 0x18);	/* SUBSRCPND */
}

static void __init homtom_machine_init(void)
{
	pm_power_off = homtom_power_off;
	homtom_lcd_gpio();
	homtom_display_mark("10_machine_init", HOMTOM_COL_TEAL);

	gpio_request_one(S3C2410_GPJ(14), GPIOF_OUT_INIT_LOW, "movi-pwr");
	s3c_sdhci1_set_platdata(&homtom_hsmmc1_pdata);

	s3c24xx_hsudc_set_platdata(&homtom_hsudc_platdata);
	{
		struct s3c24xx_hsudc_platdata *pd =
			s3c_device_usb_hsudc.dev.platform_data;
		if (pd && !pd->phy_init) {
			pd->phy_init = homtom_hsudc_phy_init;
			pd->phy_uninit = homtom_hsudc_phy_uninit;
		}
	}
	platform_add_devices(homtom_devices, ARRAY_SIZE(homtom_devices));
	homtom_display_mark("11_post_devices", HOMTOM_COL_ORANGE);
}

static int __init homtom_display_mark_late(void)
{
	homtom_display_mark("12_late", HOMTOM_COL_WHITE);
	return 0;
}
late_initcall(homtom_display_mark_late);

MACHINE_START(TOMTOMGO, "homtom GO 630")
	.atag_offset	= 0x100,
	.nr_irqs	= NR_IRQS_S3C2443,
	.init_irq	= homtom_init_irq,
	.reserve	= homtom_reserve,
	.map_io		= homtom_map_io,
	.restart	= homtom_restart,
	.init_machine	= homtom_machine_init,
	.init_time	= homtom_init_time,
MACHINE_END
