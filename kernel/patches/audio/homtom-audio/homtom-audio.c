// SPDX-License-Identifier: GPL-2.0
/*
 * homtom-audio: gapless speaker playback for the GO 630 (S3C2443 IIS, WM8750/WM8971 codec) through one S3C DMA channel.
 *
 * /dev/homtom-audio takes 16-bit little-endian stereo frames (4 bytes each, left in the low halfword). The frames go into
 * a ring buffer in coherent memory. One physical DMA channel runs in auto-reload mode over the whole ring, paced by the
 * IIS TX FIFO request (DMAREQSEL source 4), so the hardware loops the ring with no CPU work and no gap. A 1-jiffy timer
 * reads the channel's current source address, zeroes what the DMA has fetched (unwritten parts of the ring play as
 * silence) and wakes the writer. No DMA interrupt is used.
 *
 * Why not dmaengine: the s3c24xx-dma driver (CONFIG_S3C24XX_DMAC=y, device s3c2443-dma.0 registered by the board) asks
 * for per-channel clocks "dma.0".."dma.5", and clk-s3c2443.c registers no such lookups for non-DT boards, so every
 * physical channel is marked invalid at probe. The driver has no clients on this board (UART DMA is DT-only, the SDHCI
 * and HSUDC blocks have their own DMA), so a channel driven directly cannot collide with it. See docs/AUDIO_DMA.md.
 *
 * Codec (L3 bit-bang on GPB2-4), DAC power (GPJ15) and amplifier (GPG11) stay in userland (tools/devtools/homtom-play).
 * This module touches: IIS block, one DMA channel, GPECON[9:0] (GPE0-4 to I2S), HCLKCON bit <ch>, PCLKCON bit 9,
 * SCLKCON bit 9, CLKSRC[15:14]. Never EPLLCON, CLKSRC[8:6], CLKDIV1, INFORM0, GPJ or GPL.
 *
 * mode=0: IIS master, MCLK = EPLL reference (12 MHz crystal) / 1, 256fs, so fs = 46875 Hz (homtom-snd2 setup).
 * mode=1: IIS slave, CDCLK = 12 MHz still output to the codec; the codec is master in USB mode and sets fs
 *         (48000 or 44118 Hz). Needs the matching codec setup from homtom-play --codec-master.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/sched/signal.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#define CLK_PA		0x4C000000UL
#define DMA_PA		0x4B000000UL
#define IIS_PA		0x55000000UL
#define GPIO_PA		0x56000000UL

#define EPLLCON		0x18
#define CLKSRC		0x20
#define CLKDIV1		0x28
#define HCLKCON		0x30
#define PCLKCON		0x34
#define SCLKCON		0x38

#define IISCON		0x00
#define IISMOD		0x04
#define IISFIC		0x08
#define IISPSR		0x0C
#define IISTXD		0x10

#define IISCON_I2SACTIVE	(1u << 0)
#define IISCON_TXDMACTIVE	(1u << 2)
#define IISMOD_LR_RLOW		(1u << 7)
#define IISMOD_IMS_SYSMUX	(1u << 10)
#define IISMOD_SLAVE		(1u << 11)
#define IISFIC_TFLUSH		(1u << 15)
#define IISPSR_PSREN		(1u << 15)

#define DISRC		0x00
#define DISRCC		0x04
#define DIDST		0x08
#define DIDSTC		0x0C
#define DCON		0x10
#define DSTAT		0x14
#define DCSRC		0x18
#define DMASKTRIG	0x20
#define DMAREQSEL	0x24

#define DCON_HANDSHAKE	(1u << 31)
#define DCON_DSZ_WORD	(2u << 20)
#define DIDSTC_APB_FIXED 3u
#define DMASKTRIG_ON	(1u << 1)
#define DMASKTRIG_STOP	(1u << 2)
#define REQSEL_IIS_TX	((4u << 1) | 1u)

#define GPECON		0x40

#define XTI_HZ		12000000u
#define GUARD		1024u
#define LEAD		4096u

static int ch = 5;
module_param(ch, int, 0444);
MODULE_PARM_DESC(ch, "physical DMA channel 0-5 (default 5, the last one the dmaengine allocator would pick)");

static unsigned int ring_kb = 64;
module_param(ring_kb, uint, 0444);
MODULE_PARM_DESC(ring_kb, "ring size in KiB, power of two 8-256 (default 64 = 350 ms at 46875 Hz)");

static int mode;
module_param(mode, int, 0644);
MODULE_PARM_DESC(mode, "0 = IIS master 12 MHz/256 (46875 Hz), 1 = IIS slave, codec master; read at open");

static bool demand;
module_param(demand, bool, 0644);
MODULE_PARM_DESC(demand, "DMA demand mode like stock 2.6.13 instead of handshake (mainline); read at open");

static bool force;
module_param(force, bool, 0644);
MODULE_PARM_DESC(force, "open even if IISCON shows the IIS already active (e.g. a crashed homtom-snd2)");

static struct {
	void __iomem *clk, *dma, *iis, *gpio;
	struct platform_device *pdev;
	struct clk *epll, *pclk;
	void *ring;
	dma_addr_t phys;
	u32 size;
	spinlock_t lock;
	struct mutex wlock;
	wait_queue_head_t wq;
	struct timer_list timer;
	unsigned long busy;
	bool running, primed, meas;
	int run_mode;
	u32 last_off, done, wr;
	u64 total, b0, b1;
	ktime_t t0, t1;
	u32 underruns, bad_pos;
} ha;

static u32 ha_epllref_hz(void)
{
	u32 src = readl(ha.clk + CLKSRC);

	switch ((src >> 7) & 3) {
	case 0:
	case 1:
		return (src & (1u << 3)) ? 0 : XTI_HZ;
	case 2:
		return XTI_HZ;
	default:
		return 0;
	}
}

static void ha_rmw(void __iomem *r, u32 mask, u32 val)
{
	writel((readl(r) & ~mask) | (val & mask), r);
}

static void ha_zero(u32 abs, u32 len)
{
	u32 off = abs & (ha.size - 1), n = min(len, ha.size - off);

	memset(ha.ring + off, 0, n);
	if (len > n)
		memset(ha.ring, 0, len - n);
}

static void ha_tick(struct timer_list *t)
{
	unsigned long fl;
	u32 a, off, d;

	spin_lock_irqsave(&ha.lock, fl);
	if (!ha.running) {
		spin_unlock_irqrestore(&ha.lock, fl);
		return;
	}
	a = readl(ha.dma + DCSRC);
	off = a - (u32)ha.phys;
	if (off >= ha.size) {
		ha.bad_pos++;
		off = ha.last_off;
	}
	off &= ~3u;
	d = (off - ha.last_off) & (ha.size - 1);
	if (d) {
		ha_zero(ha.done, d);
		ha.done += d;
		ha.last_off = off;
		ha.total += d;
		if (!ha.meas) {
			ha.meas = true;
			ha.t0 = ktime_get();
			ha.b0 = ha.total;
		}
		ha.t1 = ktime_get();
		ha.b1 = ha.total;
	}
	spin_unlock_irqrestore(&ha.lock, fl);
	wake_up_interruptible(&ha.wq);
	mod_timer(&ha.timer, jiffies + 1);
}

static void ha_dma_stop(void)
{
	int i;

	writel(DMASKTRIG_STOP, ha.dma + DMASKTRIG);
	for (i = 0; i < 1000 && (readl(ha.dma + DMASKTRIG) & DMASKTRIG_ON); i++)
		udelay(1);
}

static void ha_hw_start(void)
{
	unsigned long fl;
	u32 mod = IISMOD_LR_RLOW | IISMOD_IMS_SYSMUX;

	local_irq_save(fl);
	ha_rmw(ha.clk + HCLKCON, 1u << ch, 1u << ch);
	ha_rmw(ha.clk + PCLKCON, 1u << 9, 1u << 9);
	ha_rmw(ha.clk + SCLKCON, 1u << 9, 1u << 9);
	ha_rmw(ha.clk + CLKSRC, 3u << 14, 2u << 14);
	ha_rmw(ha.gpio + GPECON, 0x3ff, 0x2aa);
	local_irq_restore(fl);

	if (ha.run_mode == 1)
		mod |= IISMOD_SLAVE;
	writel(0, ha.iis + IISCON);
	writel(mod, ha.iis + IISMOD);
	writel(IISFIC_TFLUSH, ha.iis + IISFIC);
	writel(0, ha.iis + IISFIC);
	writel(IISPSR_PSREN, ha.iis + IISPSR);

	ha_dma_stop();
	writel((u32)ha.phys, ha.dma + DISRC);
	writel(0, ha.dma + DISRCC);
	writel(IIS_PA + IISTXD, ha.dma + DIDST);
	writel(DIDSTC_APB_FIXED, ha.dma + DIDSTC);
	writel((demand ? 0 : DCON_HANDSHAKE) | DCON_DSZ_WORD | (ha.size / 4), ha.dma + DCON);
	writel(REQSEL_IIS_TX, ha.dma + DMAREQSEL);

	spin_lock_irqsave(&ha.lock, fl);
	ha.last_off = 0;
	ha.done = ha.wr = 0;
	ha.total = ha.b0 = ha.b1 = 0;
	ha.meas = ha.primed = false;
	ha.underruns = ha.bad_pos = 0;
	ha.running = true;
	spin_unlock_irqrestore(&ha.lock, fl);

	writel(DMASKTRIG_ON, ha.dma + DMASKTRIG);
	writel(IISCON_TXDMACTIVE | IISCON_I2SACTIVE, ha.iis + IISCON);
	mod_timer(&ha.timer, jiffies + 1);
}

static void ha_hw_stop(void)
{
	unsigned long fl;

	spin_lock_irqsave(&ha.lock, fl);
	ha.running = false;
	spin_unlock_irqrestore(&ha.lock, fl);
	del_timer_sync(&ha.timer);
	writel(0, ha.iis + IISCON);
	ha_dma_stop();
	writel(IISFIC_TFLUSH, ha.iis + IISFIC);
	writel(0, ha.iis + IISFIC);
	wake_up_interruptible(&ha.wq);
}

static u32 ha_queued(void)
{
	unsigned long fl;
	s32 q;

	spin_lock_irqsave(&ha.lock, fl);
	q = (s32)(ha.wr - ha.done);
	spin_unlock_irqrestore(&ha.lock, fl);
	return q > 0 ? q : 0;
}

static bool ha_writable(void)
{
	return !ha.running || ha_queued() + GUARD < ha.size;
}

static int ha_drain(void)
{
	long t = msecs_to_jiffies(ha.size / 128 + 500);
	unsigned long fl;

	t = wait_event_interruptible_timeout(ha.wq, !ha.running || !ha_queued(), t);
	spin_lock_irqsave(&ha.lock, fl);
	ha.primed = false;
	spin_unlock_irqrestore(&ha.lock, fl);
	return t < 0 ? (int)t : 0;
}

static ssize_t ha_write(struct file *f, const char __user *buf, size_t n, loff_t *ppos)
{
	size_t got = 0;
	ssize_t ret = 0;

	n &= ~(size_t)3;
	if (mutex_lock_interruptible(&ha.wlock))
		return -ERESTARTSYS;
	while (got < n) {
		unsigned long fl;
		u32 wr, used, space, off, chunk;

		spin_lock_irqsave(&ha.lock, fl);
		if (!ha.running) {
			spin_unlock_irqrestore(&ha.lock, fl);
			ret = -EIO;
			break;
		}
		if (!ha.primed || (s32)(ha.wr - ha.done) < 0) {
			if (ha.primed)
				ha.underruns++;
			ha.wr = ha.done + min(LEAD, ha.size / 4);
			ha.primed = true;
		}
		wr = ha.wr;
		used = wr - ha.done;
		spin_unlock_irqrestore(&ha.lock, fl);

		space = used + GUARD < ha.size ? ha.size - GUARD - used : 0;
		if (!space) {
			if (f->f_flags & O_NONBLOCK) {
				ret = -EAGAIN;
				break;
			}
			if (wait_event_interruptible(ha.wq, ha_writable())) {
				ret = -ERESTARTSYS;
				break;
			}
			continue;
		}
		off = wr & (ha.size - 1);
		chunk = min3((u32)(n - got), space, ha.size - off);
		if (copy_from_user(ha.ring + off, buf + got, chunk)) {
			ret = -EFAULT;
			break;
		}
		wmb();
		spin_lock_irqsave(&ha.lock, fl);
		if (ha.wr == wr)
			ha.wr = wr + chunk;
		spin_unlock_irqrestore(&ha.lock, fl);
		got += chunk;
	}
	mutex_unlock(&ha.wlock);
	return got ? (ssize_t)got : ret;
}

static int ha_fsync(struct file *f, loff_t start, loff_t end, int datasync)
{
	return ha_drain();
}

static int ha_open(struct inode *ino, struct file *f)
{
	if (test_and_set_bit(0, &ha.busy))
		return -EBUSY;
	if (mode != 0 && mode != 1)
		goto busy;
	if ((readl(ha.iis + IISCON) & IISCON_I2SACTIVE) && !force) {
		pr_warn("homtom-audio: IIS already active (IISCON=%08x); another player? load with force=1 to override\n",
			readl(ha.iis + IISCON));
		goto busy;
	}
	if (readl(ha.dma + DMASKTRIG) & DMASKTRIG_ON) {
		pr_warn("homtom-audio: DMA channel %d busy (DMASKTRIG=%08x)\n", ch, readl(ha.dma + DMASKTRIG));
		goto busy;
	}
	if (mode == 0 && ha_epllref_hz() != XTI_HZ)
		pr_warn("homtom-audio: EPLL reference is not the 12 MHz crystal (CLKSRC=%08x); fs_nominal is wrong\n",
			readl(ha.clk + CLKSRC));
	memset(ha.ring, 0, ha.size);
	ha.run_mode = mode;
	ha_hw_start();
	return 0;
busy:
	clear_bit(0, &ha.busy);
	return -EBUSY;
}

static int ha_release(struct inode *ino, struct file *f)
{
	if (!signal_pending(current))
		ha_drain();
	ha_hw_stop();
	clear_bit(0, &ha.busy);
	return 0;
}

static ssize_t status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	unsigned long fl;
	u64 frames, ns, fs = 0;
	u32 q, ur, bad, nominal = 0;
	bool run;

	spin_lock_irqsave(&ha.lock, fl);
	run = ha.running;
	frames = ha.total / 4;
	q = (s32)(ha.wr - ha.done) > 0 ? ha.wr - ha.done : 0;
	ur = ha.underruns;
	bad = ha.bad_pos;
	ns = ha.meas ? ktime_to_ns(ktime_sub(ha.t1, ha.t0)) : 0;
	if (ns > 100000000ULL)
		fs = div64_u64((ha.b1 - ha.b0) / 4 * 1000000000ULL, ns);
	spin_unlock_irqrestore(&ha.lock, fl);
	if (ha.run_mode == 0)
		nominal = ha_epllref_hz() / 256;

	return sysfs_emit(buf,
		"running=%d mode=%d ch=%d ring=%u fs_nominal=%u fs_measured=%llu meas_ms=%llu frames=%llu queued=%u "
		"underruns=%u bad_pos=%u epll_hz=%lu pclk_hz=%lu\n"
		"CLKSRC=%08x CLKDIV1=%08x EPLLCON=%08x HCLKCON=%08x IISCON=%08x IISMOD=%08x IISPSR=%08x "
		"DCON=%08x DSTAT=%08x DCSRC=%08x DMASKTRIG=%08x\n",
		run, ha.run_mode, ch, ha.size, nominal, fs, div_u64(ns, 1000000), frames, q, ur, bad,
		IS_ERR_OR_NULL(ha.epll) ? 0 : clk_get_rate(ha.epll), IS_ERR_OR_NULL(ha.pclk) ? 0 : clk_get_rate(ha.pclk),
		readl(ha.clk + CLKSRC), readl(ha.clk + CLKDIV1), readl(ha.clk + EPLLCON), readl(ha.clk + HCLKCON),
		readl(ha.iis + IISCON), readl(ha.iis + IISMOD), readl(ha.iis + IISPSR),
		readl(ha.dma + DCON), readl(ha.dma + DSTAT), readl(ha.dma + DCSRC), readl(ha.dma + DMASKTRIG));
}
static DEVICE_ATTR_RO(status);

static struct attribute *ha_attrs[] = {
	&dev_attr_status.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ha);

static const struct file_operations ha_fops = {
	.owner		= THIS_MODULE,
	.open		= ha_open,
	.release	= ha_release,
	.write		= ha_write,
	.fsync		= ha_fsync,
	.llseek		= no_llseek,
};

static struct miscdevice ha_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "homtom-audio",
	.fops	= &ha_fops,
	.groups	= ha_groups,
	.mode	= 0600,
};

static void ha_unmap(void)
{
	if (ha.gpio)
		iounmap(ha.gpio);
	if (ha.iis)
		iounmap(ha.iis);
	if (ha.dma)
		iounmap(ha.dma);
	if (ha.clk)
		iounmap(ha.clk);
}

static int __init ha_init(void)
{
	u32 kb;
	int ret;

	if (ch < 0 || ch > 5)
		return -EINVAL;
	kb = clamp(ring_kb, 8u, 256u);
	kb = 1u << (fls(kb) - 1);
	spin_lock_init(&ha.lock);
	mutex_init(&ha.wlock);
	init_waitqueue_head(&ha.wq);
	timer_setup(&ha.timer, ha_tick, 0);

	ha.clk = ioremap(CLK_PA, 0x100);
	ha.dma = ioremap(DMA_PA + ch * 0x100, 0x100);
	ha.iis = ioremap(IIS_PA, 0x100);
	ha.gpio = ioremap(GPIO_PA, 0x100);
	if (!ha.clk || !ha.dma || !ha.iis || !ha.gpio) {
		ret = -ENOMEM;
		goto err_map;
	}
	ha.pdev = platform_device_register_simple("homtom-audio", -1, NULL, 0);
	if (IS_ERR(ha.pdev)) {
		ret = PTR_ERR(ha.pdev);
		goto err_map;
	}
	for (; kb >= 8; kb >>= 1) {
		ha.ring = dma_alloc_coherent(&ha.pdev->dev, kb * 1024, &ha.phys, GFP_KERNEL);
		if (ha.ring)
			break;
	}
	if (!ha.ring) {
		ret = -ENOMEM;
		goto err_pdev;
	}
	ha.size = kb * 1024;
	ha.epll = clk_get(NULL, "epll");
	ha.pclk = clk_get(NULL, "timers");
	ret = misc_register(&ha_misc);
	if (ret)
		goto err_ring;
	pr_info("homtom-audio: ch %d ring %u B at %pad, epll %lu Hz, pclk %lu Hz, epllref %u Hz, CLKSRC=%08x EPLLCON=%08x\n",
		ch, ha.size, &ha.phys, IS_ERR(ha.epll) ? 0 : clk_get_rate(ha.epll),
		IS_ERR(ha.pclk) ? 0 : clk_get_rate(ha.pclk), ha_epllref_hz(),
		readl(ha.clk + CLKSRC), readl(ha.clk + EPLLCON));
	return 0;

err_ring:
	if (!IS_ERR(ha.epll))
		clk_put(ha.epll);
	if (!IS_ERR(ha.pclk))
		clk_put(ha.pclk);
	dma_free_coherent(&ha.pdev->dev, ha.size, ha.ring, ha.phys);
err_pdev:
	platform_device_unregister(ha.pdev);
err_map:
	ha_unmap();
	return ret;
}

static void __exit ha_exit(void)
{
	misc_deregister(&ha_misc);
	if (ha.running)
		ha_hw_stop();
	if (!IS_ERR(ha.epll))
		clk_put(ha.epll);
	if (!IS_ERR(ha.pclk))
		clk_put(ha.pclk);
	dma_free_coherent(&ha.pdev->dev, ha.size, ha.ring, ha.phys);
	platform_device_unregister(ha.pdev);
	ha_unmap();
}

module_init(ha_init);
module_exit(ha_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GO 630 speaker playback through one S3C2443 DMA channel");
