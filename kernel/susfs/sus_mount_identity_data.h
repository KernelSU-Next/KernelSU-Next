/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __SUS_MOUNT_IDENTITY_DATA_H
#define __SUS_MOUNT_IDENTITY_DATA_H

#include <linux/spinlock.h>
#include <linux/atomic.h>
#include "sus_mount_identity_values.h"

extern struct sus_mount_ident sus_mount_idents[SUS_MOUNT_IDENT_MAX];
extern int sus_mount_n_ident;
extern spinlock_t sus_mount_ident_lock;
extern atomic_t sus_mount_ident_full_count;
extern atomic_t sus_mount_ident_drop_count;

void sus_mount_ident_drop_dev(dev_t s_dev);
void sus_mount_ident_reset(void);
int sus_mount_ident_live(void);
int sus_mount_ident_used(void);
int sus_mount_ident_full(void);
int sus_mount_ident_dropped_dev(void);

#endif
