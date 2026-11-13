// SPDX-License-Identifier: GPL-2.0
/*
 * printk console drawn straight into the TomTom GO 630 LCD framebuffer
 * (480x272 RGB565, scanned from LCDSADDR1 << 1). No fbdev driver needed.
 *
 * The write path runs under console_lock, or with the lock busted during
 * panic. It takes no locks, never allocates or sleeps, and touches no
 * __init code or data, so it stays valid after free_initmem().
 */
#include <linux/console.h>
#include <linux/font.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/types.h>
#include <asm/cacheflush.h>
#include <asm/memory.h>

/* 8 → VGA 8x8, 60x34 cells; 16 → VGA 8x16, 60x17 cells. */
#ifndef HOMTOM_FBTEXT_FONT_H
#define HOMTOM_FBTEXT_FONT_H	8
#endif
/* First pixel row owned by the console; rows above it are left alone. */
#ifndef HOMTOM_FBTEXT_Y0
#define HOMTOM_FBTEXT_Y0	0
#endif
/*
 * On the first write seen with oops_in_progress set, move the last two lines
 * (the "Unable to handle ..." and pgd lines printed before die()) to the top,
 * clear the rest, and stop drawing once the screen is full, so the head of the
 * oops (PC/LR, registers, Code:) survives for a photo instead of scrolling off.
 */
#ifndef HOMTOM_FBTEXT_FREEZE_ON_OOPS
#define HOMTOM_FBTEXT_FREEZE_ON_OOPS	1
#endif
#define FBT_OOPS_KEEP		2

#define FBT_FG			0xFFFFu

#if HOMTOM_FBTEXT_FONT_H == 8
# if defined(CONFIG_FONT_8x8) && IS_BUILTIN(CONFIG_FONT_SUPPORT)
#  define FBT_FONT		font_vga_8x8
# else
#  define font_vga_8x8		homtom_fbtext_font
#  include "../../../lib/fonts/font_8x8.c"
#  undef font_vga_8x8
#  define FBT_FONT		homtom_fbtext_font
# endif
#elif HOMTOM_FBTEXT_FONT_H == 16
# if defined(CONFIG_FONT_8x16) && IS_BUILTIN(CONFIG_FONT_SUPPORT)
#  define FBT_FONT		font_vga_8x16
# else
#  define font_vga_8x16		homtom_fbtext_font
#  include "../../../lib/fonts/font_8x16.c"
#  undef font_vga_8x16
#  define FBT_FONT		homtom_fbtext_font
# endif
#else
# error "HOMTOM_FBTEXT_FONT_H must be 8 or 16"
#endif

#define FBT_LCD			((void __iomem *)0xf7500000UL)
#define FBT_LCDSADDR1		0x14
#define FBT_TPAL		0x50
#define FBT_RAM_START		0x30000000UL
#define FBT_RAM_END		0x34000000UL

#define FBT_W			480
#define FBT_H			272
#define FBT_FH			HOMTOM_FBTEXT_FONT_H
#define FBT_COLS		(FBT_W / 8)
#define FBT_ROWS		((FBT_H - HOMTOM_FBTEXT_Y0) / FBT_FH)
#define FBT_ROW_BYTES		(FBT_FH * FBT_W * 2)

void homtom_fbtext_init(void);
void homtom_fbtext_reset_cursor(void);

/* Two pixels per u32; font bit 1 is the left (lower-address) pixel. */
static const u32 fbt_lut[4] = {
	0, FBT_FG << 16, FBT_FG, FBT_FG | (FBT_FG << 16),
};

/* Boot splash (480x272 RGB565, brand/splash). Text console is verbose by default; "homtom.quiet" on the command line or /sys/kernel/homtom_splash=1 shows the splash and mutes text until an oops. */
asm(".section .rodata,\"a\"\n.balign 4\nhomtom_splash_fb:\n.incbin \"arch/arm/kernel/homtom-splash.fb\"\n.previous\n");
extern const u8 homtom_splash_fb[FBT_W * FBT_H * 2];

static u16 *fbt_fb;
static bool fbt_quiet;
static const u8 *fbt_glyphs;
static unsigned int fbt_col, fbt_row;
static unsigned int fbt_dirty_lo, fbt_dirty_hi;
static bool fbt_oops, fbt_frozen;

static u16 *fbt_rowp(unsigned int row)
{
	return fbt_fb + (HOMTOM_FBTEXT_Y0 + row * FBT_FH) * FBT_W;
}

static void fbt_mark(unsigned int first, unsigned int n)
{
	unsigned int lo = HOMTOM_FBTEXT_Y0 + first * FBT_FH;
	unsigned int hi = lo + n * FBT_FH;

	if (lo < fbt_dirty_lo)
		fbt_dirty_lo = lo;
	if (hi > fbt_dirty_hi)
		fbt_dirty_hi = hi;
}

static void fbt_clear_rows(unsigned int first, unsigned int n)
{
	if (!n)
		return;
	memset(fbt_rowp(first), 0, n * FBT_ROW_BYTES);
	fbt_mark(first, n);
}

static bool fbt_newline(void)
{
	fbt_col = 0;
	if (fbt_row + 1 < FBT_ROWS) {
		fbt_row++;
		fbt_clear_rows(fbt_row, 1);
		return true;
	}
	if (HOMTOM_FBTEXT_FREEZE_ON_OOPS && fbt_oops) {
		fbt_frozen = true;
		return false;
	}
	memmove(fbt_rowp(0), fbt_rowp(1), (FBT_ROWS - 1) * FBT_ROW_BYTES);
	fbt_clear_rows(FBT_ROWS - 1, 1);
	fbt_mark(0, FBT_ROWS);
	return true;
}

static void fbt_glyph(unsigned char c)
{
	const u8 *g = fbt_glyphs + c * FBT_FH;
	u32 *p = (u32 *)(fbt_rowp(fbt_row) + fbt_col * 8);
	unsigned int y;

	for (y = 0; y < FBT_FH; y++, p += FBT_W / 2) {
		u8 b = g[y];

		p[0] = fbt_lut[b >> 6];
		p[1] = fbt_lut[(b >> 4) & 3];
		p[2] = fbt_lut[(b >> 2) & 3];
		p[3] = fbt_lut[b & 3];
	}
	fbt_mark(fbt_row, 1);
	fbt_col++;
}

static bool fbt_put(unsigned char c)
{
	if (fbt_col >= FBT_COLS && !fbt_newline())
		return false;
	fbt_glyph(c);
	return true;
}

static void fbt_keep_tail(void)
{
	unsigned int first = fbt_row > FBT_OOPS_KEEP ? fbt_row - FBT_OOPS_KEEP : 0;
	unsigned int n = fbt_row - first + 1;

	if (first)
		memmove(fbt_rowp(0), fbt_rowp(first), n * FBT_ROW_BYTES);
	fbt_clear_rows(n, FBT_ROWS - n);
	fbt_row = n - 1;
	fbt_mark(0, FBT_ROWS);
}

static void fbt_write(struct console *con, const char *s, unsigned int n)
{
	if (!fbt_fb)
		return;
	if (fbt_quiet) {
		if (!oops_in_progress)
			return;
		fbt_quiet = false;
	}
	/* A nested oops inside this function can leave the cursor mid-update. */
	if (fbt_row >= FBT_ROWS)
		fbt_row = FBT_ROWS - 1;
	if (fbt_col > FBT_COLS)
		fbt_col = FBT_COLS;
	fbt_dirty_lo = FBT_H;
	fbt_dirty_hi = 0;

	if (HOMTOM_FBTEXT_FREEZE_ON_OOPS && oops_in_progress && !fbt_oops) {
		fbt_oops = true;
		fbt_keep_tail();
	}

	while (n-- && !fbt_frozen) {
		unsigned char c = *s++;

		if (c == '\n') {
			fbt_newline();
		} else if (c == '\r') {
			fbt_col = 0;
		} else if (c == '\t') {
			do {
				if (!fbt_put(' '))
					break;
			} while (fbt_col & 7);
		} else if (c >= 0x20) {
			fbt_put(c < 0x7f ? c : '?');
		}
	}

	if (fbt_dirty_hi > fbt_dirty_lo)
		__cpuc_flush_dcache_area(fbt_fb + fbt_dirty_lo * FBT_W,
					 (fbt_dirty_hi - fbt_dirty_lo) * FBT_W * 2);
}

static struct console fbt_console = {
	.name	= "homtom",
	.write	= fbt_write,
	.flags	= CON_PRINTBUFFER | CON_ENABLED,
	.index	= -1,
};

/*
 * Call once, after paging_init() (lowmem and the static LCD mapping must
 * exist). Not __init, so a late caller is fine too.
 */
void homtom_fbtext_init(void)
{
	unsigned long phys;

	if (fbt_fb)
		return;
	phys = (unsigned long)__raw_readl(FBT_LCD + FBT_LCDSADDR1) << 1;
	if (phys < FBT_RAM_START || phys > FBT_RAM_END - FBT_W * FBT_H * 2)
		return;

	__raw_writel(0, FBT_LCD + FBT_TPAL);
	fbt_glyphs = FBT_FONT.data;
	fbt_fb = (u16 *)phys_to_virt(phys);
	if (!strstr(boot_command_line, "homtom.quiet")) {
		memset(fbt_rowp(0), 0, FBT_ROWS * FBT_ROW_BYTES);
		__cpuc_flush_dcache_area(fbt_rowp(0), FBT_ROWS * FBT_ROW_BYTES);
	} else {
		fbt_quiet = true;
		memcpy(fbt_fb, homtom_splash_fb, FBT_W * FBT_H * 2);
		__cpuc_flush_dcache_area(fbt_fb, FBT_W * FBT_H * 2);
	}
	fbt_col = 0;
	fbt_row = 0;

	register_console(&fbt_console);
}

void homtom_fbtext_splash(bool on)
{
	if (!fbt_fb)
		return;
	if (on) {
		fbt_quiet = true;
		memcpy(fbt_fb, homtom_splash_fb, FBT_W * FBT_H * 2);
		__cpuc_flush_dcache_area(fbt_fb, FBT_W * FBT_H * 2);
	} else {
		fbt_quiet = false;
	}
}

/*
 * Home the cursor so the next console line lands on the top row. Call it with
 * console_lock held (or where nothing else can be printing) to avoid racing
 * fbt_write(); a race only misplaces text, the cursor is clamped on entry.
 */
void homtom_fbtext_reset_cursor(void)
{
	fbt_col = 0;
	fbt_row = 0;
}
