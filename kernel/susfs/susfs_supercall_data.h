/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_supercall_data.h - supercall registry and state (D layer)
 */

#ifndef __SUSFS_SUPERCALL_DATA_H
#define __SUSFS_SUPERCALL_DATA_H

#include <linux/types.h>
#include "susfs_abi.h"
#include "susfs_fp_hook.h"

struct feature_entry {
	const char *name;
	int feature;		/* enum susfs_feature, or -1 = always present */
};

extern const struct feature_entry susfs_feature_table[];
extern const unsigned int susfs_feature_table_count;

extern struct st_susfs_enabled_features susfs_enabled_features_nomem;
extern susfs_syscall_fn_t susfs_reboot_orig;
extern bool susfs_sc_fp_armed;
extern bool susfs_sc_kp_armed;

void susfs_supercall_arm_feature(enum susfs_feature feature);
bool susfs_supercall_feature_armed(enum susfs_feature feature);

#endif /* __SUSFS_SUPERCALL_DATA_H */
