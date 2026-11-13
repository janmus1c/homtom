/*
 * Homtom GO 630: solid cyan via LCD TPAL + TFT WIN0MAP.
 * Runs in compressed/head.S before stack setup — no sp, preserve r7/r8/r9.
 * Local literal pool (no ldr=) so we do not depend on a distant .ltorg.
 */
	b	.Lht_after_lits
.Lht_hclk:	.word	0x4C000030
.Lht_misccr:	.word	0x5600000C
.Lht_tpal:	.word	0x4D000050
.Lht_saddr1:	.word	0x4D000014
.Lht_win0:	.word	0x4C8000D0
.Lht_win1:	.word	0x4C8000D4
.Lht_vidcon0:	.word	0x4C800000
.Lht_mapcol:	.word	0x0118BCF2
.Lht_rgb565:	.word	0x000034BD
.Lht_dram0:	.word	0x30000000
.Lht_dram1:	.word	0x34000000
.Lht_pixels:	.word	(480 * 272)
.Lht_after_lits:
	ldr	r0, .Lht_hclk
	ldr	r1, [r0]
	orr	r1, r1, #(1 << 9)
	str	r1, [r0]
	ldr	r0, .Lht_misccr
	ldr	r1, [r0]
	orr	r1, r1, #(1 << 28)
	str	r1, [r0]
	ldr	r0, .Lht_tpal
	ldr	r1, .Lht_mapcol
	str	r1, [r0]
	ldr	r0, .Lht_saddr1
	ldr	r2, [r0]
	mov	r2, r2, lsl #1
	ldr	r3, .Lht_dram0
	cmp	r2, r3
	blo	.Lht_tft
	ldr	r3, .Lht_dram1
	cmp	r2, r3
	bhs	.Lht_tft
	ldr	r3, .Lht_rgb565
	ldr	r4, .Lht_pixels
.Lht_lcd_loop:
	strh	r3, [r2], #2
	subs	r4, r4, #1
	bne	.Lht_lcd_loop
.Lht_tft:
	ldr	r0, .Lht_win0
	ldr	r1, .Lht_mapcol
	str	r1, [r0]
	ldr	r0, .Lht_win1
	str	r1, [r0]
	ldr	r0, .Lht_vidcon0
	ldr	r1, [r0]
	orr	r1, r1, #3
	str	r1, [r0]
