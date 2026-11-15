/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SUSFS_OPEN_REDIRECT_H
#define __SUSFS_OPEN_REDIRECT_H

#include <linux/types.h>
#include <linux/compiler.h>
struct path;

int susfs_open_redirect_init(void);
void susfs_open_redirect_exit(void);

void susfs_open_redirect_supercall(void __user **arg);

/* open_redirect, reverse direction, for callers that only have an inode NUMBER: fdinfo prints "ino:\t<i>" with no device, so
 * this is a lookup by ino alone and returns false when the number is ambiguous (two rules, same redirected ino) or the caller is
 * not one the reverse disguise applies to.  On success *out_ino is the target inode to show, *out_mnt_id the target mount id. */
bool susfs_open_redirect_spoof_ids(unsigned long ino, unsigned long *out_ino, unsigned long *out_mnt_id);

/* The width of a rule's stored pathnames (the ABI fields are char[256], and matching that width stops
 * a legal long path from being truncated into a rule for a different path). */
#define SUSFS_OR_PATH_MAX 256

/* Asked by the name-layer entry hook (susfs_or_entry.c) for the path a caller is about to open.
 * The redirect target is COPIED into the caller's buffer (buflen >= SUSFS_OR_PATH_MAX) while the
 * lookup runs under rcu_read_lock, and the return value is false when no live rule matches (or the
 * name does not fit).  A pointer into the rule could not be handed out any more: rule slots are
 * recycled now (a deleted rule's slot is handed out again once a grace period has passed), so
 * nothing inside an entry may be read after the lookup returns. */
bool susfs_open_redirect_match_path(const struct path *path, char *buf, size_t buflen);
bool susfs_open_redirect_in_su_domain(void);

/* How many rules are live right now.  The name-layer hook runs for every open() on the system, so it
 * tests this before it resolves anything - a table with no live rule costs one load instead of a path
 * lookup, and deleting the last rule takes that cost away again.  Written under the rule lock. */
extern unsigned int susfs_open_redirect_rules;

static inline bool susfs_open_redirect_active(void)
{
	return READ_ONCE(susfs_open_redirect_rules) != 0;
}

#endif /* __SUSFS_OPEN_REDIRECT_H */
