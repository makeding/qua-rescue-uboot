/* SPDX-License-Identifier: GPL-2.0 */
/* Original AArch32 U-Boot: dcache off, then go this RAM trampoline. */
.syntax unified
.arm
.text
.global _start
_start:
 push {r4-r11, lr}
 mrc p15, 0, r4, c1, c0, 0
 tst r4, #4                    /* Caller must clean/disable D-cache first. */
 movne r0, #1
 bne fail
 /* Independent physical bypass before candidate validation/copy.
  * ISO31 is the active-low WPS input. Never change its direction here. */
 ldr r5, =0x98007100
 ldr r6, [r5]
 tst r6, #0x80000000
 bne 9f
 ldr r6, [r5, #8]
 tst r6, #0x80000000
 moveq r0, #4
 beq fail
9:
 adr r10, parameters
 ldmia r10, {r0-r3, r11}       /* src, length, CRC32, destination, magic */
 ldr r5, =0x51554254
 cmp r11, r5
 bne invalid
 cmp r1, #0
 beq invalid
 cmp r1, #0x100000
 bhi invalid
 ldr r5, =0x20000000
 cmp r0, r5
 blo invalid
 adds r5, r0, r1
 bcs invalid
 ldr r6, =0x80000000
 cmp r5, r6
 bhi invalid
 ldr r5, =0x21000
 cmp r3, r5
 bne invalid
 mvn r5, #0
 mov r6, r0
 mov r7, r1
 ldr r9, =0xedb88320
1: ldrb r8, [r6], #1
 eor r5, r5, r8
 mov r8, #8
2: lsrs r5, r5, #1
 eorcs r5, r5, r9
 subs r8, r8, #1
 bne 2b
 subs r7, r7, #1
 bne 1b
 mvn r5, r5
 cmp r5, r2
 movne r0, #3
 bne fail
 /* From here there is no return; original code at destination is overwritten. */
 cpsid if
 dsb sy
 bic r4, r4, #1               /* MMU off; D-cache already off. */
 bic r4, r4, #0x1000          /* I-cache off. */
 mcr p15, 0, r4, c1, c0, 0
 isb sy
 ldr sp, =0x01efff00
 mov r6, r3
3: ldrb r7, [r0], #1
 strb r7, [r6], #1
 subs r1, r1, #1
 bne 3b
 mov r0, #0
 dsb sy
 mcr p15, 0, r0, c7, c5, 0   /* Invalidate instruction cache. */
 mcr p15, 0, r0, c7, c5, 6   /* Invalidate branch predictor. */
 isb sy
 bx r3
invalid:
 mov r0, #2
fail:
 pop {r4-r11, pc}
.ltorg
.balign 16
parameters:
 .word 0x26000000, 0, 0, 0x21000, 0x51554254
