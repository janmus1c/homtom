/*
 * FIQ PC sampler (tick-independent): fires from the S3C2443 watchdog used as an interval timer, routed to FIQ.
 * Draws the interrupted PC+4 (lr_fiq) and CPSR (spsr_fiq) as 32 white/black 8x8 squares each (MSB left)
 * straight into the framebuffer (phys 0x33F00000 = VA 0xC3F00000) at y=250 / y=260, then cleans those cache lines.
 * Position independent (copied into the FIQ vector by set_fiq_handler). Uses only banked r8-r14, no stack.
 */
#include <linux/linkage.h>
	.text
	.global	homtom_fiq_start
	.global	homtom_fiq_end
homtom_fiq_start:
	mov	r13, lr				@ keep return address (interrupted pc + 4)
	ldr	r8, =0xF6400000			@ watchdog
	mov	r9, #1
	str	r9, [r8, #0x0c]			@ WTCLRINT first: release the WDT interrupt request (else FIQ storm)
	ldr	r8, =0xF6000000			@ interrupt controller
	mov	r9, #0x2000
	str	r9, [r8, #0x18]			@ SUBSRCPND: clear INT_WDT (bit 13)
	mov	r9, #0x200
	str	r9, [r8, #0x00]			@ SRCPND: clear INT_WDT_AC97 (bit 9)
	ldr	r8, =(0xC3F00000 + (250 * 480 + 6) * 2)
	mov	r9, r13
	bl	homtom_fiq_draw32
	ldr	r8, =(0xC3F00000 + (260 * 480 + 6) * 2)
	mrs	r9, spsr
	bl	homtom_fiq_draw32
	ldr	r8, =(0xC3F00000 + 250 * 480 * 2)
	ldr	r9, =(20 * 480 * 2 / 32)
1:	mcr	p15, 0, r8, c7, c10, 1		@ clean D line by MVA
	add	r8, r8, #32
	subs	r9, r9, #1
	bne	1b
	mov	r9, #0
	mcr	p15, 0, r9, c7, c10, 4		@ drain write buffer
	subs	pc, r13, #4

homtom_fiq_draw32:
	mov	r10, #32
2:	tst	r9, #0x80000000
	mvnne	r11, #0
	moveq	r11, #0
	mov	r12, #8
3:	strh	r11, [r8], #2
	strh	r11, [r8], #2
	strh	r11, [r8], #2
	strh	r11, [r8], #2
	strh	r11, [r8], #2
	strh	r11, [r8], #2
	strh	r11, [r8], #2
	strh	r11, [r8], #2
	add	r8, r8, #944			@ (480 - 8) * 2
	subs	r12, r12, #1
	bne	3b
	sub	r8, r8, #7680			@ back to the top of the cell
	add	r8, r8, #28			@ next cell (14 px)
	mov	r9, r9, lsl #1
	subs	r10, r10, #1
	bne	2b
	mov	pc, lr
	.ltorg
homtom_fiq_end:
