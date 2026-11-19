/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_dirent_policy.c - getdents prefilter decision (L layer)
 */

#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/string.h>

#include "susfs_dirent_data.h"
#include "susfs_dirent_policy.h"

/* sus_path's feature interface; called, not re-declared as a layer head. */
extern bool sus_path_dirent_hidden(u64 ino, const char *name);

/* fs/readdir.c's verify_dirent_name(), which is static there - both halves of
 * it: the length bound is the one this used to drop, and the kernel applies
 * the whole check before it writes anything, which is why it is called before
 * the copy in the filldir and not after. */
bool susfs_dirent_name_ok(const char *name, int len)
{
	return len > 0 && len < PATH_MAX && !memchr(name, '/', len);
}

/* A filldir callback hands over (pointer, length), NOT a C string: the NUL the
 * kernel relies on is one IT appends in the CALLER's buffer, and a filesystem
 * that passes on raw reply bytes never had one - fuse does exactly that.
 * Copy it out and terminate it first; a name that cannot be one is not matched
 * here and falls into the caller's name check. */
bool susfs_dirent_should_hide(const char *name, int namlen, u64 ino)
{
	char nbuf[NAME_MAX + 1];

	if (namlen <= 0 || namlen > NAME_MAX)
		return false;
	memcpy(nbuf, name, namlen);
	nbuf[namlen] = 0;
	if (!sus_path_dirent_hidden(ino, nbuf))
		return false;
	atomic_inc(&susfs_dirent_hidden);
	return true;
}

bool susfs_dirent_prefilter_enabled(void)
{
	return READ_ONCE(susfs_dirent_prefilter_on) && READ_ONCE(susfs_dirent_armed) &&
	       READ_ONCE(susfs_dirent_iterate_dir) && READ_ONCE(susfs_dirent_fget);
}
