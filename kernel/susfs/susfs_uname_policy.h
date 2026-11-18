/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __SUSFS_UNAME_POLICY_H
#define __SUSFS_UNAME_POLICY_H

#include "susfs_uname_view.h"

struct susfs_hook;

bool susfs_uname_snapshot(long result, bool has_buffer, struct susfs_uname_values *out);
void susfs_uname_policy_init(struct susfs_hook *hook);
void susfs_uname_policy_exit(struct susfs_hook *hook);
int susfs_uname_set(struct susfs_hook *hook, const struct susfs_uname_values *input);

#endif
