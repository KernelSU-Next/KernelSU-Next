/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/string.h>
#include "susfs_hide_data.h"

char hide_modules[HIDE_MODULES_MAX][MODULE_NAME_LEN];
char hide_modules_applied[HIDE_MODULES_MAX][MODULE_NAME_LEN];	/* sysfs rules live */
int n_hide_modules;
int n_hide_modules_applied;
DEFINE_SPINLOCK(hide_modules_lock);

atomic_t n_modlines_skipped = ATOMIC_INIT(0);	/* /proc/modules lines removed */
atomic_t n_kallsyms_mod_skipped = ATOMIC_INIT(0);	/* kallsyms lines of those modules */
atomic_t n_sysfs_rules_added = ATOMIC_INIT(0);
atomic_t n_sysfs_rules_failed = ATOMIC_INIT(0);

char hide_modules_pending_val[HIDE_MODULES_CMDLINE];
bool hide_modules_pending_given;
bool hide_syms_ready;
DEFINE_MUTEX(hide_syms_param_lock);

atomic_t hide_hit_count = ATOMIC_INIT(0);
atomic_t hide_enter_count = ATOMIC_INIT(0);
atomic_t hide_layout_bad = ATOMIC_INIT(0);
bool hide_module_name_match(const char *name)
{
	unsigned long flags;
	bool hit = false;
	int i;

	if (!name || !name[0])
		return false;

	spin_lock_irqsave(&hide_modules_lock, flags);
	for (i = 0; i < n_hide_modules; i++) {
		if (!strcmp(hide_modules[i], name)) {
			hit = true;
			break;
		}
	}
	spin_unlock_irqrestore(&hide_modules_lock, flags);
	return hit;
}

void hide_modules_commit(char dst[][MODULE_NAME_LEN], int n)
{
	unsigned long flags;

	spin_lock_irqsave(&hide_modules_lock, flags);
	memset(hide_modules, 0, sizeof(hide_modules));
	memcpy(hide_modules, dst, (size_t)n * MODULE_NAME_LEN);
	n_hide_modules = n;
	spin_unlock_irqrestore(&hide_modules_lock, flags);
}
