/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Runs from 0x01f02000 after cleanup_before_bootcode().  It has no stack,
 * makes no firmware or DDR changes, and must never return: destination
 * 0x21000 is the currently executing first-stage U-Boot image.
 */
.syntax unified
.arm
.text
.global _start
_start:
	cpsid	if
	/* cleanup_before_bootcode() must have cleaned and disabled D-cache.
	 * Do not copy from RAM if a dirty cache can still hide source bytes. */
	mrc	p15, 0, r12, c1, c0, 0
	tst	r12, #4			/* SCTLR.C */
	bne	3f
	/* The rescue build normally starts cache-off, but make the final
	 * execution state explicit before touching the replacement image. */
	bic	r12, r12, #1		/* SCTLR.M */
	bic	r12, r12, #0x1000	/* SCTLR.I */
	mcr	p15, 0, r12, c1, c0, 0
	dsb	sy
	isb	sy
	ldr	r0, =0x01500000	/* staged, verified vendor U-Boot64 */
	ldr	r1, =0x00021000	/* vendor goldenboot destination */
	ldr	r2, =0x0000b2000	/* exact observed golden copy size */

1:
	cmp	r2, #0
	beq	2f
	ldmia	r0!, {r3-r10}
	stmia	r1!, {r3-r10}
	subs	r2, r2, #32
	b	1b

2:
	/* cleanup_before_bootcode() already cleaned D-cache and disabled the
	 * MMU/caches.  Publish copied instructions before the vendor EL3 entry. */
	dsb	sy
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0	/* invalidate I-cache */
	mcr	p15, 0, r0, c7, c5, 6	/* invalidate branch predictor */
	dsb	sy
	isb	sy
	.word	0xe1600070		/* smc #0: exact vendor goldenboot entry */

3:
	b	3b			/* cache-state rejection or unexpected SMC return */
.ltorg
