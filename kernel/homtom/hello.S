@ ARMv4T EABI static probe, run as /init. Reports through its exit code (PID 1 exit => kernel panic text
@ "Attempted to kill init! exitcode=0x0000XX00" on the LCD console), independent of /dev/kmsg:
@   exit(errno)  open("/dev/kmsg") failed (2=ENOENT, 19=ENODEV, 13=EACCES, ...)
@   exit(100+n)  write failed, n = errno
@   exit(200)    open+write fine => line "homtom-hello: ..." must also be on the console
@ Set HELLO_EXEC to exec busybox sh /init.sh instead of exiting once the probe is trusted.
	.text
	.global	_start
_start:
	ldr	r0, =kmsg
	mov	r1, #1			@ O_WRONLY
	mov	r7, #5			@ open
	swi	0
	cmp	r0, #0
	rsblt	r0, r0, #0		@ exit(errno)
	blt	do_exit
	mov	r4, r0
	mov	r0, r4
	ldr	r1, =msg1
	mov	r2, #(msg1end - msg1)
	mov	r7, #4			@ write
	swi	0
	cmp	r0, #0
	rsblt	r0, r0, #0
	addlt	r0, r0, #100
	blt	do_exit
	@ stdin/stdout/stderr -> /dev/kmsg so errors from the loader/busybox show on the LCD console
	mov	r0, r4
	mov	r1, #1
	mov	r7, #63			@ dup2
	swi	0
	mov	r0, r4
	mov	r1, #2
	mov	r7, #63
	swi	0
#ifdef HELLO_EXEC
	ldr	r0, =bb
	ldr	r1, =argv
	ldr	r2, =envp
	mov	r7, #11			@ execve
	swi	0
	mov	r0, #199		@ execve failed
	b	do_exit
#else
	mov	r0, #200
#endif
do_exit:
	mov	r7, #1			@ exit
	swi	0
1:	b	1b
	.ltorg
	.section .rodata
kmsg:	.asciz	"/dev/kmsg"
bb:	.asciz	"/bin/busybox"
a0:	.asciz	"busybox"
a1:	.asciz	"sh"
a2:	.asciz	"/init.sh"
msg1:	.ascii	"homtom-hello: userspace alive (ARMv4T probe)\n"
msg1end:
	.align	2
argv:	.word	a0, a1, a2, 0
envp:	.word	0
	.section .note.GNU-stack,"",%progbits
