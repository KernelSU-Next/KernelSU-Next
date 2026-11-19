/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SUSFS_SYMBOL_RESOLVER_H
#define __SUSFS_SYMBOL_RESOLVER_H

void *ksu_resolve_symbol_for_functable_hook(const char *symbol_name);
unsigned long find_kernel_symbol_exact(const char *symbol_name);
unsigned long find_kernel_symbol_exact_any(const char *symbol_name);
int ksu_find_symbol_all(const char *name, unsigned long *addrs, int max);
unsigned long ksu_kallsyms_lookup_name(const char *name);
int ksu_symbol_name_of(unsigned long addr, char *buf, char **module_out);
int ksu_symbol_is_start(unsigned long addr, const char *name);
int ksu_addr_is_named(unsigned long addr, const char *name);
int ksu_init_symbol_resolver(void);

#endif
