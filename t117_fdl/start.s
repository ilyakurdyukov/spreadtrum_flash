@ -*- tab-width: 8 -*-
	.arch armv7-a
	.syntax unified

.macro CODE16_FN name
	.section .text.\name, "ax", %progbits
	.p2align 1
	.code 16
	.type \name, %function
	.global \name
	.thumb_func
\name:
.endm

.macro CODE32_FN name
	.section .text.\name, "ax", %progbits
	.p2align 2
	.code 32
	.type \name, %function
	.global \name
\name:
.endm

//  0 : MMU enable
//  2 : Level 1 Data Cache enable
// 12 : Instruction Cache enable

CODE32_FN _start
	.ascii "DHTB"
	.long 1
	.org _start + 0x30, 0
	.long __image_size
	.org _start + 0x200, 0

	adr	r2, _start
	ldr	r0, 3f
4:	cmp	r2, r0
	bne	4b

	mrc	p15, #0, r0, c1, c0, #0 // Read Control Register
	bic	r0, #5
.if 1
	bic	r0, #0x1000
.else // faster
	orr	r0, #0x1000
.endif
	mcr	p15, #0, r0, c1, c0, #0 // Write Control Register

	mov	r0, #0xf00000
	mcr	p15, #0, r0, c1, c0, #2
	mov	r0, #0x40000000
	vmsr	FPEXC, r0
	isb	sy
	mov	r2, #0xd3
	msr	cpsr_c, r2
	mov	r0, #0x12000
	mov	sp, r0
	blx	init_chip
	mov	sp, r0

	// 24 - FZ (flush to zero)
	// 25 - DN (default NaN)
	mov	r0, #0x3000000
	vmsr	FPSCR, r0

	ldr	pc, 2f
2:	.long	entry_main
3:	.long	__image_start

CODE32_FN __gnu_thumb1_case_uqi
	bic	r12, lr, #1
	ldrb	r12, [r12, r0]
	add	lr, lr, r12, lsl #1
	bx	lr

CODE32_FN __gnu_thumb1_case_uhi
	add	r12, r0, lr, lsr #1
	ldrh	r12, [r12, r12]
	add	lr, lr, r12, lsl #1
	bx	lr

// TODO: cache line size for UMS9117 is 64

CODE32_FN clean_dcache_range
	mrc	p15, #0, r3, c0, c0, #1
	mov	r2, #4
	ubfx	r3, r3, #16, #4
	lsl	r2, r3
	sub	r3, r2, #1
	bic	r0, r3
1:	mcr	p15, #0, r0, c7, c10, #1
	add	r0, r2
	cmp	r0, r1
	blo	1b
	dsb	sy
	bx	lr

CODE32_FN invalidate_dcache_range
	mrc	p15, #0, r3, c0, c0, #1
	mov	r2, #4
	ubfx	r3, r3, #16, #4
	lsl	r2, r3
	sub	r3, r2, #1
	tst	r0, r3
	bic	r0, r3
	mcrne	p15, #0, r0, c7, c14, #1 // clean and invalidate
	tst	r1, r3
	bic	r1, r3
	mcrne	p15, #0, r1, c7, c14, #1 // clean and invalidate
1:	mcr	p15, #0, r0, c7, c6, #1
	add	r0, r2
	cmp	r0, r1
	blo	1b
	dsb	sy
	bx	lr

