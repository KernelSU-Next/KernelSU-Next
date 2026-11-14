/* SPDX-License-Identifier: GPL-2.0 */
/*
 * susfs_hide_stub.S - PAC-safe entry stubs for the s_show/m_show inline hooks.
 *
 * Both bodies start with a PAC prologue.  A C wrapper detour would replay
 * paciasp with the wrapper's frame and the body's autiasp would then fail
 * (the vfs_open FPAC).  These stubs run with the target's own SP/LR, call the
 * decision with every caller-saved register saved, and either return the
 * hidden answer (0) or restore the frame and jump to the trampoline.
 */

#define SUSFS_SAVE_REGS()					\
	sub	sp, sp, #160;					\
	stp	x0,  x1,  [sp, #0];				\
	stp	x2,  x3,  [sp, #16];				\
	stp	x4,  x5,  [sp, #32];				\
	stp	x6,  x7,  [sp, #48];				\
	stp	x8,  x9,  [sp, #64];				\
	stp	x10, x11, [sp, #80];				\
	stp	x12, x13, [sp, #96];				\
	stp	x14, x15, [sp, #112];				\
	stp	x16, x17, [sp, #128];				\
	stp	x18, x30, [sp, #144]

#define SUSFS_RESTORE_REGS()					\
	ldp	x0,  x1,  [sp, #0];				\
	ldp	x2,  x3,  [sp, #16];				\
	ldp	x4,  x5,  [sp, #32];				\
	ldp	x6,  x7,  [sp, #48];				\
	ldp	x8,  x9,  [sp, #64];				\
	ldp	x10, x11, [sp, #80];				\
	ldp	x12, x13, [sp, #96];				\
	ldp	x14, x15, [sp, #112];				\
	ldp	x16, x17, [sp, #128];				\
	ldp	x18, x30, [sp, #144];				\
	add	sp, sp, #160

	.text

	.globl	susfs_hide_s_stub
	.type	susfs_hide_s_stub, %function
susfs_hide_s_stub:
	SUSFS_SAVE_REGS()
	bl	hide_syms_s_show_decide
	cbz	w0, .Ls_allow
	SUSFS_RESTORE_REGS()
	mov	x0, #0			/* hidden: s_show returns 0 */
	ret
.Ls_allow:
	SUSFS_RESTORE_REGS()
	ldr	x16, =susfs_hide_s_tramp
	ldr	x16, [x16]
	ret	x16
	.size	susfs_hide_s_stub, . - susfs_hide_s_stub

	.globl	susfs_hide_m_stub
	.type	susfs_hide_m_stub, %function
susfs_hide_m_stub:
	SUSFS_SAVE_REGS()
	bl	hide_syms_m_show_decide
	cbz	w0, .Lm_allow
	SUSFS_RESTORE_REGS()
	mov	x0, #0			/* hidden: m_show returns 0 */
	ret
.Lm_allow:
	SUSFS_RESTORE_REGS()
	ldr	x16, =susfs_hide_m_tramp
	ldr	x16, [x16]
	ret	x16
	.size	susfs_hide_m_stub, . - susfs_hide_m_stub

	.bss
	.align	3
	.globl	susfs_hide_s_tramp
susfs_hide_s_tramp:
	.quad	0
	.globl	susfs_hide_m_tramp
susfs_hide_m_tramp:
	.quad	0
