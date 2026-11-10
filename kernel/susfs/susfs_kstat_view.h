/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_kstat_view.h - value-only view for the kstat read decision
 *
 * F/P fill the view; L decides whether the stat read may enter the spoof
 * table.  P still owns the table and the user-buffer rewrite.
 */

#ifndef __SUSFS_KSTAT_VIEW_H
#define __SUSFS_KSTAT_VIEW_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t u32;
#endif

enum susfs_kstat_action {
	SUSFS_KSTAT_SKIP,
	SUSFS_KSTAT_LOOKUP,
};

struct susfs_kstat_view {
	bool table_empty;
	bool caller_is_umounted_app;
	bool rule_matched;
	bool buffer_ok;
	u32 spoof_flags;
};

static inline bool
susfs_kstat_gate_allowed(const struct susfs_kstat_view *v)
{
	return v && !v->table_empty && v->caller_is_umounted_app;
}

static inline enum susfs_kstat_action
susfs_kstat_decide_core(const struct susfs_kstat_view *v)
{
	if (!susfs_kstat_gate_allowed(v))
		return SUSFS_KSTAT_SKIP;
	if (!v->rule_matched || !v->spoof_flags)
		return SUSFS_KSTAT_SKIP;
	return SUSFS_KSTAT_LOOKUP;
}

#endif /* __SUSFS_KSTAT_VIEW_H */
