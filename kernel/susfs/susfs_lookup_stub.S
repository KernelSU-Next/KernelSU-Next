/* SPDX-License-Identifier: GPL-2.0 */

	.macro	SUSFS_SAVE_ARGS
	sub	sp, sp, #176
	stp	x0,  x1,  [sp, #0]
	stp	x2,  x3,  [sp, #16]
	stp	x4,  x5,  [sp, #32]
	stp	x6,  x7,  [sp, #48]
	stp	x8,  x9,  [sp, #64]
	stp	x10, x11, [sp, #80]
	stp	x12, x13, [sp, #96]
	stp	x14, x15, [sp, #112]
	stp	x16, x17, [sp, #128]
	stp	x18, x30, [sp, #144]
	.endm

	.text

	.globl	susfs_path_lookupat_stub
	.type	susfs_path_lookupat_stub, %function
susfs_path_lookupat_stub:
	SUSFS_SAVE_ARGS
	ldr	x16, =susfs_path_lookupat_tramp
	ldr	x16, [x16]
	blr	x16
	str	x0, [sp, #160]
	mov	x0, sp
	bl	susfs_path_lookupat_decide
	ldr	x30, [sp, #152]
	add	sp, sp, #176
	ret
	.size	susfs_path_lookupat_stub, . - susfs_path_lookupat_stub

	.globl	susfs_filename_lookup_stub
	.type	susfs_filename_lookup_stub, %function
susfs_filename_lookup_stub:
	SUSFS_SAVE_ARGS
	ldr	x16, =susfs_filename_lookup_tramp
	ldr	x16, [x16]
	blr	x16
	str	x0, [sp, #160]
	mov	x0, sp
	bl	susfs_filename_lookup_decide
	ldr	x30, [sp, #152]
	add	sp, sp, #176
	ret
	.size	susfs_filename_lookup_stub, . - susfs_filename_lookup_stub

	.globl	susfs_path_openat_stub
	.type	susfs_path_openat_stub, %function
susfs_path_openat_stub:
	SUSFS_SAVE_ARGS
	ldr	x16, =susfs_path_openat_tramp
	ldr	x16, [x16]
	blr	x16
	str	x0, [sp, #160]
	mov	x0, sp
	bl	susfs_path_openat_decide
	ldr	x30, [sp, #152]
	add	sp, sp, #176
	ret
	.size	susfs_path_openat_stub, . - susfs_path_openat_stub

	.bss
	.align	3
	.globl	susfs_path_lookupat_tramp
susfs_path_lookupat_tramp:
	.quad	0
	.globl	susfs_filename_lookup_tramp
susfs_filename_lookup_tramp:
	.quad	0
	.globl	susfs_path_openat_tramp
susfs_path_openat_tramp:
	.quad	0
