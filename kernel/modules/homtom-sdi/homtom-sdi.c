// SPDX-License-Identifier: GPL-2.0
/*
 * GO 630 SD slot: legacy S3C SDI controller (0x5A000000, IRQ 37) bound to the in-kernel s3cmci driver as "s3c2440-sdi".
 * Pins (stock 2.6.13 tomtomgo-iomodena.h): data/clk/cmd GPE5-10 (function 2), card detect GPF1 (low = present),
 * write protect GPF7 (not used here), card power SD_PWR_ON GPJ11 (high = on).
 * Loadable so the slot can be developed without rebooting: insmod homtom-sdi.ko
 */
#include <linux/delay.h>
#include <linux/gpio/machine.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/platform_data/mmc-s3cmci.h>
#include <linux/mmc/host.h>

#define GPIO_BASE	0x56000000UL
#define GPECON		0x40
#define GPJCON		0xd0
#define GPJDAT		0xd4

static void __iomem *gpio;

static void homtom_sd_set_power(unsigned char power_mode, unsigned short vdd)
{
	u32 v;

	if (!gpio)
		return;
	switch (power_mode) {
	case MMC_POWER_UP:
	case MMC_POWER_ON:
		v = readl(gpio + GPJCON);
		writel((v & ~(3u << 22)) | (1u << 22), gpio + GPJCON);
		writel(readl(gpio + GPJDAT) | (1u << 11), gpio + GPJDAT);
		v = readl(gpio + GPECON);
		writel((v & ~0x003ffc00u) | 0x002aa800u, gpio + GPECON);
		break;
	default:
		writel(readl(gpio + GPJDAT) & ~(1u << 11), gpio + GPJDAT);
		break;
	}
}

static struct s3c24xx_mci_pdata homtom_sd_pdata = {
	.no_wprotect	= 1,
	.ocr_avail	= MMC_VDD_32_33 | MMC_VDD_33_34,
	.set_power	= homtom_sd_set_power,
};

static struct gpiod_lookup_table homtom_sd_gpios = {
	.dev_id = "s3c2440-sdi",
	.table = {
		GPIO_LOOKUP("GPIOF", 1, "cd", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("GPIOE", 5, "bus", 0, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("GPIOE", 6, "bus", 1, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("GPIOE", 7, "bus", 2, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("GPIOE", 8, "bus", 3, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("GPIOE", 9, "bus", 4, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("GPIOE", 10, "bus", 5, GPIO_ACTIVE_HIGH),
		{ },
	},
};

static struct resource homtom_sd_res[] = {
	DEFINE_RES_MEM(0x5a000000, SZ_1M),
	DEFINE_RES_IRQ(37),
};

static struct platform_device *homtom_sd_dev;

static int __init homtom_sdi_init(void)
{
	gpio = ioremap(GPIO_BASE, SZ_4K);
	if (!gpio)
		return -ENOMEM;
	gpiod_add_lookup_table(&homtom_sd_gpios);
	homtom_sd_dev = platform_device_register_resndata(NULL, "s3c2440-sdi", -1, homtom_sd_res,
							  ARRAY_SIZE(homtom_sd_res), &homtom_sd_pdata,
							  sizeof(homtom_sd_pdata));
	if (IS_ERR(homtom_sd_dev)) {
		gpiod_remove_lookup_table(&homtom_sd_gpios);
		iounmap(gpio);
		return PTR_ERR(homtom_sd_dev);
	}
	return 0;
}

static void __exit homtom_sdi_exit(void)
{
	platform_device_unregister(homtom_sd_dev);
	gpiod_remove_lookup_table(&homtom_sd_gpios);
	iounmap(gpio);
}

module_init(homtom_sdi_init);
module_exit(homtom_sdi_exit);
MODULE_LICENSE("GPL");
