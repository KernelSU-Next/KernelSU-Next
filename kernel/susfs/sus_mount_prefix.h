/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sus_mount_prefix.h - the hide-mount prefix records
 */

#ifndef __SUS_MOUNT_PREFIX_H
#define __SUS_MOUNT_PREFIX_H

#include <linux/types.h>

#define SUS_MOUNT_KEEP_MAX 64
#define SUS_MOUNT_KEEP_LEN 128
#define SUS_MOUNT_KEEP_CMDLINE (SUS_MOUNT_KEEP_MAX * (SUS_MOUNT_KEEP_LEN + 2) + 32)

int sus_mount_prefix_commit(char dst[][SUS_MOUNT_KEEP_LEN], int n);
int sus_mount_prefix_snapshot(char dst[][SUS_MOUNT_KEEP_LEN]);
int sus_mount_prefix_count(void);
bool sus_mount_prefix_match(const char *s);
const char *sus_mount_prefix_default(void);

#endif /* __SUS_MOUNT_PREFIX_H */
