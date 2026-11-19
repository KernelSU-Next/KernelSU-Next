/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sus_mount_policy.h - sus_mount hide decisions
 */

#ifndef __SUS_MOUNT_POLICY_H
#define __SUS_MOUNT_POLICY_H

#include <linux/types.h>

struct mount;

typedef bool (*sus_mount_prefix_fn)(const char *devname);

bool __nocfi sus_mount_is_ours(struct mount *r, unsigned int id_min,
                               sus_mount_prefix_fn prefix);
int __nocfi sus_mount_shown_id_from(struct mount *mnt, unsigned int id_min,
                                    sus_mount_prefix_fn prefix);

#endif /* __SUS_MOUNT_POLICY_H */
