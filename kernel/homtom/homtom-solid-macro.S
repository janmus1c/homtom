/*
 * Homtom GO 630 boot color ladder (TPAL / WIN0MAP), MMU-off safe.
 *
 * Stub (HOMTOM_SOLID): 1 cyan … 6 red
 * Kernel head pairs: 6a … 6d (see COLOR_LADDER.txt)
 *
 * MMU probe (distinguishes CPU halt vs TPAL freeze):
 *   HOMTOM_PROBE_PRE  — fill scanout FB lime (#80FF00), cover with TPAL orange
 *   HOMTOM_PROBE_POST — clear TPAL/WIN0MAP (LCD DMA already shows FB → lime)
 *   If the panel goes lime after the c1 write, the CPU ran past MMU enable.
 *   If it stays orange, the CPU died in/before c1 OR post-MMU IO is unreachable.
 *
 * Post-MMU paints (6h+) need the LCD/CLK/GPIO identity map from
 * apply-kernel-ladder (VA==PA). Do not assume bare physical MMIO works.
 *
 * HOMTOM_SOLID / HOMTOM_PAIR clobber r0,r1 only.
 * HOMTOM_SOLID_TPAL — LCD TPAL only (no TFT WIN*/MISCCR; post-MMU safe).
 * HOMTOM_PROBE_PRE clobbers r0–r4. HOMTOM_PROBE_POST clobbers r0,r1.
 */
	.macro	HOMTOM_SOLID, color, tag
	.ifdef HOMTOM_COLOR_LADDER
	b	.Lht_after_\tag
.Lht_hclk_\tag:		.word	0x4C000030
.Lht_misccr_\tag:	.word	0x5600000C
.Lht_tpal_\tag:		.word	0x4D000050
.Lht_win0_\tag:		.word	0x4C8000D0
.Lht_win1_\tag:		.word	0x4C8000D4
.Lht_vidcon0_\tag:	.word	0x4C800000
.Lht_mapcol_\tag:	.word	(0x01000000 | \color)
.Lht_after_\tag:
	ldr	r0, .Lht_hclk_\tag
	ldr	r1, [r0]
	orr	r1, r1, #(1 << 9)
	str	r1, [r0]
	ldr	r0, .Lht_misccr_\tag
	ldr	r1, [r0]
	orr	r1, r1, #(1 << 28)
	str	r1, [r0]
	ldr	r0, .Lht_tpal_\tag
	ldr	r1, .Lht_mapcol_\tag
	str	r1, [r0]
	ldr	r0, .Lht_win0_\tag
	str	r1, [r0]
	ldr	r0, .Lht_win1_\tag
	str	r1, [r0]
	ldr	r0, .Lht_vidcon0_\tag
	ldr	r1, [r0]
	orr	r1, r1, #3
	str	r1, [r0]
	.endif
	.endm

	/* Post-MMU: legacy LCD TPAL only. Avoids 0x4C800000 TFT bus hangs. */
	.macro	HOMTOM_SOLID_TPAL, color, tag
	.ifdef HOMTOM_COLOR_LADDER
	b	.Lht_tafter_\tag
.Lht_thclk_\tag:	.word	0x4C000030
.Lht_ttpal_\tag:	.word	0x4D000050
.Lht_tcol_\tag:		.word	(0x01000000 | \color)
.Lht_tafter_\tag:
	ldr	r0, .Lht_thclk_\tag
	ldr	r1, [r0]
	orr	r1, r1, #(1 << 9)
	str	r1, [r0]
	ldr	r0, .Lht_ttpal_\tag
	ldr	r1, .Lht_tcol_\tag
	str	r1, [r0]
	.endif
	.endm

	.macro	HOMTOM_PAIR, color_a, color_b, tag
	.ifdef HOMTOM_COLOR_LADDER
	b	.Lht_pafter_\tag
.Lht_phclk_\tag:	.word	0x4C000030
.Lht_pmisccr_\tag:	.word	0x5600000C
.Lht_ptpal_\tag:	.word	0x4D000050
.Lht_pwin0_\tag:	.word	0x4C8000D0
.Lht_pwin1_\tag:	.word	0x4C8000D4
.Lht_pvidcon0_\tag:	.word	0x4C800000
.Lht_pcola_\tag:	.word	(0x01000000 | \color_a)
.Lht_pcolb_\tag:	.word	(0x01000000 | \color_b)
.Lht_pdelay_\tag:	.word	0x02000000
.Lht_pafter_\tag:
	ldr	r0, .Lht_phclk_\tag
	ldr	r1, [r0]
	orr	r1, r1, #(1 << 9)
	str	r1, [r0]
	ldr	r0, .Lht_pmisccr_\tag
	ldr	r1, [r0]
	orr	r1, r1, #(1 << 28)
	str	r1, [r0]
	ldr	r0, .Lht_pvidcon0_\tag
	ldr	r1, [r0]
	orr	r1, r1, #3
	str	r1, [r0]
	ldr	r1, .Lht_pcola_\tag
	ldr	r0, .Lht_ptpal_\tag
	str	r1, [r0]
	ldr	r0, .Lht_pwin0_\tag
	str	r1, [r0]
	ldr	r0, .Lht_pwin1_\tag
	str	r1, [r0]
	ldr	r0, .Lht_pdelay_\tag
.Lht_pwait_\tag:
	subs	r0, r0, #1
	bne	.Lht_pwait_\tag
	ldr	r1, .Lht_pcolb_\tag
	ldr	r0, .Lht_ptpal_\tag
	str	r1, [r0]
	ldr	r0, .Lht_pwin0_\tag
	str	r1, [r0]
	ldr	r0, .Lht_pwin1_\tag
	str	r1, [r0]
	.endif
	.endm

	/*
	 * Pre-MMU: paint FB lime (RGB565 0x87E0), then TPAL orange overlay.
	 * LCD DMA keeps scanning FB under the overlay.
	 */
	.macro	HOMTOM_PROBE_PRE, tag
	b	.Lht_pr_after_\tag
.Lht_pr_hclk_\tag:	.word	0x4C000030
.Lht_pr_misccr_\tag:	.word	0x5600000C
.Lht_pr_tpal_\tag:	.word	0x4D000050
.Lht_pr_saddr1_\tag:	.word	0x4D000014
.Lht_pr_win0_\tag:	.word	0x4C8000D0
.Lht_pr_win1_\tag:	.word	0x4C8000D4
.Lht_pr_vidcon0_\tag:	.word	0x4C800000
.Lht_pr_orange_\tag:	.word	0x01FF8000
.Lht_pr_lime565_\tag:	.word	0x000087E0
.Lht_pr_dram0_\tag:	.word	0x30000000
.Lht_pr_dram1_\tag:	.word	0x34000000
.Lht_pr_pixels_\tag:	.word	(480 * 272)
.Lht_pr_after_\tag:
	ldr	r0, .Lht_pr_hclk_\tag
	ldr	r1, [r0]
	orr	r1, r1, #(1 << 9)
	str	r1, [r0]
	ldr	r0, .Lht_pr_misccr_\tag
	ldr	r1, [r0]
	orr	r1, r1, #(1 << 28)
	str	r1, [r0]
	ldr	r0, .Lht_pr_saddr1_\tag
	ldr	r2, [r0]
	mov	r2, r2, lsl #1
	ldr	r3, .Lht_pr_dram0_\tag
	cmp	r2, r3
	blo	.Lht_pr_overlay_\tag
	ldr	r3, .Lht_pr_dram1_\tag
	cmp	r2, r3
	bhs	.Lht_pr_overlay_\tag
	ldr	r3, .Lht_pr_lime565_\tag
	ldr	r4, .Lht_pr_pixels_\tag
.Lht_pr_loop_\tag:
	strh	r3, [r2], #2
	subs	r4, r4, #1
	bne	.Lht_pr_loop_\tag
.Lht_pr_overlay_\tag:
	ldr	r1, .Lht_pr_orange_\tag
	ldr	r0, .Lht_pr_tpal_\tag
	str	r1, [r0]
	ldr	r0, .Lht_pr_win0_\tag
	str	r1, [r0]
	ldr	r0, .Lht_pr_win1_\tag
	str	r1, [r0]
	ldr	r0, .Lht_pr_vidcon0_\tag
	ldr	r1, [r0]
	orr	r1, r1, #3
	str	r1, [r0]
	.endm

	/* Post-MMU: drop overlays so the pre-filled lime FB is what the panel shows. */
	.macro	HOMTOM_PROBE_POST, tag
	b	.Lht_po_after_\tag
.Lht_po_tpal_\tag:	.word	0x4D000050
.Lht_po_win0_\tag:	.word	0x4C8000D0
.Lht_po_win1_\tag:	.word	0x4C8000D4
.Lht_po_after_\tag:
	mov	r1, #0
	ldr	r0, .Lht_po_tpal_\tag
	str	r1, [r0]
	ldr	r0, .Lht_po_win0_\tag
	str	r1, [r0]
	ldr	r0, .Lht_po_win1_\tag
	str	r1, [r0]
	.endm
