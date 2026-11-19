/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sus_mount_idmap.h - sus_mount id -> shown-id records
 */

#ifndef __SUS_MOUNT_IDMAP_H
#define __SUS_MOUNT_IDMAP_H

#include <linux/types.h>

void sus_mount_idmap_add(int sus_id, int shown_id, dev_t s_dev);
void sus_mount_idmap_drop_dev(dev_t s_dev);
void sus_mount_idmap_drop(int sus_id);
void sus_mount_idmap_reset(void);

int sus_mount_idmap_shown_for(int sus_id);
int sus_mount_idmap_live(void);
int sus_mount_idmap_used(void);
int sus_mount_idmap_recycled(void);
int sus_mount_idmap_dropped_dev(void);
int sus_mount_idmap_full(void);

#endif /* __SUS_MOUNT_IDMAP_H */
