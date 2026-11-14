/* SPDX-License-Identifier: GPL-2.0 */
/*
 * susfs_vfs_stub.S - PAC-safe entry stub for the vfs_open hook.
 *
 * vfs_open starts with a PAC prologue on the DDK 5.15 tree.  A C wrapper cannot
 * be its detour: the trampoline would replay paciasp with the wrapper's SP and
 * LR, and the autiasp later in the body then authenticates the wrong frame
 * (measured: "Oops - FPAC" at vfs_open+0x664).
 *
 * This stub is entered with the target's own SP and LR.  It saves every
 * caller-saved register (x0-x18) as well as x29/x30: the C decision is free to
 * clobber all of them, and the trampoline then replays the original prologue
 * and falls into the rest of vfs_open, which expects its argument registers
 * intact.  Missing that save is what made the first IH attempt panic.
 *
 * The allow path leaves with `ret x16`, exempt from the BTI check.
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

	.globl	susfs_vfs_open_stub
	.type	susfs_vfs_open_stub, %function
susfs_vfs_open_stub:
	SUSFS_SAVE_REGS()
	bl	susfs_vfs_open_decide
	/* test the decision BEFORE restoring: x0 is reloaded with the target's
	 * path argument by the restore, so a cbnz after it would test the
	 * pointer and hide every open. */
	cbz	w0, .Lallow
	SUSFS_RESTORE_REGS()
	mov	x0, #-2			/* -ENOENT */
	ret
.Lallow:
	SUSFS_RESTORE_REGS()
	ldr	x16, =susfs_vfs_open_tramp
	ldr	x16, [x16]
	ret	x16
	.size	susfs_vfs_open_stub, . - susfs_vfs_open_stub

	.bss
	.align	3
	.globl	susfs_vfs_open_tramp
susfs_vfs_open_tramp:
	.quad	0
