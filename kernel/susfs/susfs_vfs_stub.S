/* SPDX-License-Identifier: GPL-2.0 */
/*
 * susfs_vfs_stub.S - PAC-safe entry stub for the vfs_open hook.
 *
 * vfs_open starts with a PAC prologue on the DDK 5.15 tree.  A C wrapper cannot
 * be its detour: the trampoline would replay paciasp with the wrapper's SP and
 * LR, and the autiasp later in the body then authenticates the wrong frame
 * (measured: "Oops - FPAC" at vfs_open+0x664).
 *
 * This stub is entered with the target's own SP and LR.  It saves both, calls
 * the C decision function, restores them, and only then either returns the
 * hidden answer or jumps to the trampoline - so the trampoline's paciasp runs
 * exactly as it would on a real call.
 *
 * The allow path leaves with `ret x16`, exempt from the BTI check.
 */

	.text

	.globl	susfs_vfs_open_stub
	.type	susfs_vfs_open_stub, %function
susfs_vfs_open_stub:
	/* 32 bytes keeps SP 16-byte aligned; x0/x1 are the target's arguments */
	sub	sp, sp, #32
	stp	x0, x1, [sp, #0]
	stp	x29, x30, [sp, #16]
	bl	susfs_vfs_open_decide
	ldp	x29, x30, [sp, #16]
	ldp	x0, x1, [sp, #0]
	add	sp, sp, #32
	cbnz	w0, .Lhide
	ldr	x16, =susfs_vfs_open_tramp
	ldr	x16, [x16]
	ret	x16
.Lhide:
	mov	x0, #-2			/* -ENOENT */
	ret
	.size	susfs_vfs_open_stub, . - susfs_vfs_open_stub

	.bss
	.align	3
	.globl	susfs_vfs_open_tramp
susfs_vfs_open_tramp:
	.quad	0
