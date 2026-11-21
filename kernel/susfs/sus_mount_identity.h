/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sus_mount_identity.h - P adapters for mount identity values
 */

#ifndef __SUS_MOUNT_IDENTITY_H
#define __SUS_MOUNT_IDENTITY_H

#include <linux/types.h>

struct mount;

void sus_mount_ident_add(struct mount *r, int dbg);
bool sus_mount_ident_match(struct mount *r);
#endif /* __SUS_MOUNT_IDENTITY_H */
