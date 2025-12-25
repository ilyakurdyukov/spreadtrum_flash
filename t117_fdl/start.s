@ -*- tab-width: 8 -*-
	.arch armv7-a
	.fpu vfpv4
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
.if 0
	.long __image_size - 0x200
	.org _start + 0x200, 0

	adr	r2, _start
.else // dummy signature
	.org _start + 0x200, 0

	adr	r2, _start
	b	9f

	.org _start + 0x210, 0
	.quad 0, 0x200, 0x234, 0x200 + 0x60
	.org _start + 0x260, 0
	.long 0, 2048, 0x01000100
	.long 0x735c69c2, 0xe075fd58, 0xcad0df27, 0x15f2aa4f
	.long 0xa21e64a8, 0x696900ac, 0xefa7090f, 0x797c9e7a
	.long 0x6f1cfc9a, 0x3fd1b908, 0x8f127e6e, 0x77ab476e
	.long 0xe5ab2d53, 0xd955b1b0, 0xbf2d8a07, 0x3506a962
	.long 0x90c4c2af, 0x8c637394, 0x7a56b0d6, 0xaa4784c1
	.long 0x97ac9952, 0x8853f713, 0xafa87044, 0x30440f86
	.long 0x99d25255, 0xaac57615, 0x14212968, 0xd10128e2
	.long 0x6a131ec0, 0x35618548, 0x041b724e, 0xa908937e
	.long 0x68b37f74, 0x6a073032, 0x03294b80, 0x4bd637e0
	.long 0x1e83ad33, 0xe2d8fcbb, 0x74c6d699, 0xe07415e3
	.long 0x0ffd5b7f, 0xe4bec84c, 0xfb3543bc, 0x43d255d4
	.long 0xcc10ff98, 0x26692098, 0xa0250298, 0xd894faf6
	.long 0xb80ee7be, 0x2198a857, 0xf3947de5, 0x64e20edb
	.long 0x3bc0c823, 0x2de2551f, 0x2739e226, 0xd52a086e
	.long 0x389bdcdf, 0x78943c85, 0x1c508c34, 0xf6782b1b
	.long 0x8006aa1e, 0xc2323454, 0xa5e24f65, 0xc7cebe0b
	.long 0x42c4b0e3, 0x141cfc98, 0xc8f4fb9a, 0x24b96f99
	.long 0xe441ae27, 0x4c939b64, 0x1b9995a4, 0x55b85278
	.long 1, ~0
	.long 0x22c37350, 0x14d52f1c, 0xe0140261, 0x503d84b9
	.long 0xafa46f54, 0x3fd9244d, 0x7f7369de, 0x0e019321
	.long 0x342bb1e7, 0xb03d0b38, 0xc666beb1, 0x781e3d57
	.long 0x0d5732ad, 0x43a9da59, 0xe90b4d33, 0xeefe4625
	.long 0x1d9dca38, 0xa8006ac2, 0x013fc263, 0x9c0d0af1
	.long 0xfc23e326, 0x2615e766, 0xc3570bc5, 0x73bf7a7b
	.long 0x044314a4, 0xe41c9542, 0xcf6f7c61, 0xba3d6623
	.long 0x1bbfb091, 0x089918ad, 0x9c1d626d, 0x9119ae11
	.long 0xe783f995, 0xc4af3490, 0x104e9136, 0x8e6572ef
	.long 0x34940cc8, 0x2a722ea1, 0xd7a1229b, 0x914445cf
	.long 0xd38c403e, 0xc9d813c0, 0x45f4a221, 0x6ad403e6
	.long 0x86c1aabc, 0x790917e4, 0x50ffe8b6, 0x7ee70179
	.long 0x7636018d, 0x3468bdb6, 0x51f16704, 0x405997ba
	.long 0x2900bc6c, 0xb6475918, 0x45a1cfac, 0x98bb853f
	.long 0xaaa0d639, 0x3adf643e, 0x2edecc92, 0xe5c53c00
	.long 0x78b06bd7, 0xa02859a8, 0x32c0c6a8, 0x71182b42
9:
.endif
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
	msr	CPSR_c, r2
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

