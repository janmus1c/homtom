/*
 * Homtom: magenta after decompress_kernel, before __enter_kernel.
 * Preserve r4 (zreladdr), r7 (mach id), r8 (atags). Stack is available
 * but keep stackless + local literals like early-cyan.
 */
	b	.Lhm_after_lits
.Lhm_hclk:	.word	0x4C000030
.Lhm_misccr:	.word	0x5600000C
.Lhm_tpal:	.word	0x4D000050
.Lhm_win0:	.word	0x4C8000D0
.Lhm_win1:	.word	0x4C8000D4
.Lhm_vidcon0:	.word	0x4C800000
.Lhm_mapcol:	.word	0x01FF00FF	@ TPAL EN + magenta
.Lhm_after_lits:
	ldr	r0, .Lhm_hclk
	ldr	r1, [r0]
	orr	r1, r1, #(1 << 9)
	str	r1, [r0]
	ldr	r0, .Lhm_misccr
	ldr	r1, [r0]
	orr	r1, r1, #(1 << 28)
	str	r1, [r0]
	ldr	r0, .Lhm_tpal
	ldr	r1, .Lhm_mapcol
	str	r1, [r0]
	ldr	r0, .Lhm_win0
	str	r1, [r0]
	ldr	r0, .Lhm_win1
	str	r1, [r0]
	ldr	r0, .Lhm_vidcon0
	ldr	r1, [r0]
	orr	r1, r1, #3
	str	r1, [r0]
