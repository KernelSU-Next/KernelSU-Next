/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SUSFS_SYMBOL_RESOLVER_H
#define __SUSFS_SYMBOL_RESOLVER_H

void *ksu_resolve_symbol_for_functable_hook(const char *symbol_name);
unsigned long find_kernel_symbol_exact(const char *symbol_name);
/* EVERY vmlinux symbol with this exact name (not just the first): a kprobe registered with
 * .symbol_name attaches to whichever match kallsyms lists first, and not every name is unique
 * (measured: `seq_show` exists four times from 6.1 on, once on 5.10/5.15).  Writes at most
 * @max addresses and returns how many were written (0 = unknown/not found). */
int ksu_find_symbol_all(const char *name, unsigned long *addrs, int max);
/* Name -> address through the bootstrapped kallsyms_lookup_name(), module-owned names included
 * (unlike find_kernel_symbol_exact(), which refuses them).  0 = unknown name or no resolver; it
 * does not sleep, so it is usable from atomic context. */
unsigned long ksu_kallsyms_lookup_name(const char *name);
int ksu_symbol_name_of(unsigned long addr, char *buf, char **module_out);
/* Non-zero iff @addr is the first instruction of a core-kernel function named @name.  Both
 * directions are asked because each has a blind spot: address -> name through kallsyms_lookup()
 * (cleaned spelling, offset 0, no module owner, and no .cfi_jt jump-table entry), and "is @addr one
 * of the addresses kallsyms has for @name", which covers the kernel that names that address after
 * another symbol (code folding / vendor alias - see lsm_hook.c).  0 = "not proven", never a guess.
 * The definitions carry __nocfi (they reach the walker through the resolver's function pointers), so
 * the attribute is not repeated here. */
int ksu_symbol_is_start(unsigned long addr, const char *name);
int ksu_addr_is_named(unsigned long addr, const char *name);
void ksu_init_symbol_resolver(void);

#endif
