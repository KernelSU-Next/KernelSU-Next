/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_enable_log_policy.h - logging switch decision (L layer)
 */

#ifndef __SUSFS_ENABLE_LOG_POLICY_H
#define __SUSFS_ENABLE_LOG_POLICY_H

#include <linux/types.h>

/* Flip the runtime switch and confirm it on the log.  @tag is appended to the
 * message (" (supercall)" etc.), or NULL for the plain form. */
void susfs_enable_log_set(bool on, const char *tag);

#endif /* __SUSFS_ENABLE_LOG_POLICY_H */
