/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sus_mount_ns_iter.h - P-owned namespace mount iterator
 */

#ifndef __SUS_MOUNT_NS_ITER_H
#define __SUS_MOUNT_NS_ITER_H

#include <linux/types.h>

struct mnt_namespace;
struct mount;
struct sus_mount_ns_iter;

/* >= 6.12 only: frame->namespace_sem is resolved by the caller. */
void sus_mount_ns_iter_set_sem(void *sem);

struct sus_mount_ns_iter *sus_mount_ns_iter_begin(struct mnt_namespace *ns);
struct mount *sus_mount_ns_iter_next(struct sus_mount_ns_iter *iter);
void sus_mount_ns_iter_end(struct sus_mount_ns_iter *iter);

#endif /* __SUS_MOUNT_NS_ITER_H */
