/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __SUSFS_HIDE_POLICY_H
#define __SUSFS_HIDE_POLICY_H
#include <linux/types.h>
int hide_modules_command(const char *val, bool bare_list);
int hide_modules_format(char *buf, size_t size);
int susfs_hide_param_set(const char *val);
int susfs_hide_config_init(void);
void susfs_hide_seed_self(void);
void susfs_hide_policy_exit(void);
bool susfs_hide_has_modules(void);
bool susfs_hide_symbol_decide(const char *name, const char *module_name);
bool susfs_hide_module_decide(const char *name);
#endif
