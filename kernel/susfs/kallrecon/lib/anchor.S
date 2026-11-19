/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * anchor.S
 *
 * Copyright (C) 2026 dere3046
 *
 * LTO links unify the sprint_symbol type to Function (kallsyms.h) and
 * redirect every C-level &sprint_symbol to the module-local .cfi_jt.
 * an asm .quad reference stays a plain ABS64 relocation, resolved by
 * the module loader to the real kernel address.
 *
 * the x86 epilogue uses RET from <linux/linkage.h>, which expands to
 * jmp __x86_return_thunk on CONFIG_RETHUNK kernels: objtool rejects a
 * hand written naked ret there, the same thunk gcc emits for the C
 * files of this module.
 */

#ifdef __x86_64__
#include <linux/linkage.h>

.section .rodata
.balign 8
kr_sprint_addr_ref:
	.quad sprint_symbol
.previous

.text
.globl kr_get_sprint_addr
.type kr_get_sprint_addr, @function
kr_get_sprint_addr:
	movq kr_sprint_addr_ref(%rip), %rax
	RET
.size kr_get_sprint_addr, .-kr_get_sprint_addr

#else

.section .rodata
.balign 8
kr_sprint_addr_ref:
	.quad sprint_symbol
.previous

.text
.globl kr_get_sprint_addr
.type kr_get_sprint_addr, %function
kr_get_sprint_addr:
	adrp x0, kr_sprint_addr_ref
	ldr x0, [x0, :lo12:kr_sprint_addr_ref]
	ret
.size kr_get_sprint_addr, .-kr_get_sprint_addr

#endif
