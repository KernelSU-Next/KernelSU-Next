/* SPDX-License-Identifier: GPL-2.0 */
/*
 * susfs_ksu_stub.S - PAC-safe entry stub for KernelSU's package observer callback.
 *
 * ksu_defer_track_throne() starts with paciasp on this tree (measured:
 * "3f 23 03 d5 5e 86 00 f8 ..."), so hk_inline_hook()'s C wrapper form is
 * refused.  This is the entry-stub form: save every caller-saved register, call
 * the C hit function (only schedule_work, no arguments of the target needed),
 * restore, then jump to the trampoline, which replays the paciasp exactly as the
 * real entry would.
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

	.globl	susfs_ksu_observer_stub
	.type	susfs_ksu_observer_stub, %function
susfs_ksu_observer_stub:
	SUSFS_SAVE_REGS()
	/* ksu_handle_inode_event(mark, mask, inode, dir, file_name, cookie):
	 * pass file_name (x4) to the C hit function so it can filter for
	 * packages.list exactly like KernelSU does. */
	mov	x0, x4
	bl	susfs_priority_observer_hit
	SUSFS_RESTORE_REGS()
	ldr	x16, =susfs_ksu_observer_tramp
	ldr	x16, [x16]
	ret	x16
	.size	susfs_ksu_observer_stub, . - susfs_ksu_observer_stub

	.bss
	.align	3
	.globl	susfs_ksu_observer_tramp
susfs_ksu_observer_tramp:
	.quad	0
