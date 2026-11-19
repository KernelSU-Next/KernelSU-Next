/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_dirent_policy.h - getdents prefilter decision (L layer)
 */

#ifndef __SUSFS_DIRENT_POLICY_H
#define __SUSFS_DIRENT_POLICY_H

#include <linux/types.h>

/* fs/readdir.c's name check, applied before anything is written. */
bool susfs_dirent_name_ok(const char *name, int len);

/* True when the callback must not emit this entry. */
bool susfs_dirent_should_hide(const char *name, int namlen, u64 ino);

/* True when the prefilter may take over a getdents call at all. */
bool susfs_dirent_prefilter_enabled(void);

#endif /* __SUSFS_DIRENT_POLICY_H */
