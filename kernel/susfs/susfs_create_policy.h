/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_create_policy.h - create gate policy
 */

#ifndef __SUSFS_CREATE_POLICY_H
#define __SUSFS_CREATE_POLICY_H

#include <linux/types.h>

#include "susfs_create_view.h"

bool susfs_create_name_hidden(int dfd, const char __user *path,
			      enum susfs_create_kind kind, bool link_old);

#endif /* __SUSFS_CREATE_POLICY_H */
