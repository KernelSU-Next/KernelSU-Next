/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_create_policy.h - create gate policy
 */

#ifndef __SUSFS_CREATE_POLICY_H
#define __SUSFS_CREATE_POLICY_H

#include <linux/types.h>

bool susfs_create_name_hidden(int dfd, const char __user *path);
int susfs_create_existing_answer(struct inode *inode);

#endif /* __SUSFS_CREATE_POLICY_H */
