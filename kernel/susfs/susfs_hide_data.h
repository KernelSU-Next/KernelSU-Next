/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __SUSFS_HIDE_DATA_H
#define __SUSFS_HIDE_DATA_H
#include <linux/atomic.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include "susfs_hide_names.h"
extern char hide_modules[HIDE_MODULES_MAX][MODULE_NAME_LEN];
extern char hide_modules_applied[HIDE_MODULES_MAX][MODULE_NAME_LEN];
extern int n_hide_modules, n_hide_modules_applied;
extern spinlock_t hide_modules_lock;
extern atomic_t n_modlines_skipped, n_kallsyms_mod_skipped;
extern atomic_t n_sysfs_rules_added, n_sysfs_rules_failed;
extern atomic_t hide_hit_count, hide_enter_count, hide_layout_bad;
extern char hide_modules_pending_val[HIDE_MODULES_CMDLINE];
extern bool hide_modules_pending_given, hide_syms_ready;
extern struct mutex hide_syms_param_lock;
bool hide_module_name_match(const char *name);
void hide_modules_commit(char dst[][MODULE_NAME_LEN], int n);
#endif
