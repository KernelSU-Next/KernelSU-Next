/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sus_mount_identity.h - sus_mount identity records
 */

#ifndef __SUS_MOUNT_IDENTITY_H
#define __SUS_MOUNT_IDENTITY_H

#include <linux/types.h>

struct mount;

void sus_mount_ident_add(struct mount *r, int dbg);
bool sus_mount_ident_match(struct mount *r);
void sus_mount_ident_drop_dev(dev_t s_dev);
void sus_mount_ident_reset(void);

int sus_mount_ident_live(void);
int sus_mount_ident_used(void);
int sus_mount_ident_full(void);
int sus_mount_ident_dropped_dev(void);

#endif /* __SUS_MOUNT_IDENTITY_H */
