/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Write the Cortex-A8 Auxiliary Control Register through the OMAP3 GP ROM
 * monitor. ACTLR is writable only from the secure world, so on a GP OMAP3
 * the kernel asks the ROM: service 3 in r12, the new value in r0, smc #0.
 * That is arch/arm/mach-omap2/sleep34xx.S (l2_inv_gp, the OFF-mode restore).
 * The ROM may clobber any register, so r4-r12 and lr are saved, as U-Boot's
 * omap3_gp_romcode_call does; its dsb/dmb before the smc are kept too.
 *
 * void ca8_ibe_rom_write_actlr(u32 val);  call with IRQs off.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>

	.arm
	.arch_extension sec

ENTRY(ca8_ibe_rom_write_actlr)
	stmfd	sp!, {r4 - r12, lr}
	mov	r12, #0x3		@ GP ROM service: write ACTLR from r0
	dsb
	dmb
	smc	#0
	isb
	ldmfd	sp!, {r4 - r12, pc}
ENDPROC(ca8_ibe_rom_write_actlr)
