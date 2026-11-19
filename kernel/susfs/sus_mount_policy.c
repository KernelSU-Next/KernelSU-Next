/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sus_mount_policy.c - sus_mount hide decisions
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/dcache.h>

#include "mount.h"
#include "sus_mount_identity.h"
#include "sus_mount_policy.h"

bool __nocfi noinline sus_mount_is_ours(struct mount *r, unsigned int id_min,
                                        sus_mount_prefix_fn prefix)
{
	if ((unsigned int)r->mnt_id >= id_min)
		return true;
	if (prefix && prefix(r->mnt_devname))
		return true;
	return sus_mount_ident_match(r);
}

int __nocfi noinline sus_mount_shown_id_from(struct mount *mnt, unsigned int id_min,
                                             sus_mount_prefix_fn prefix)
{
	if (mnt && (unsigned int)mnt->mnt_id < id_min)
		mnt = mnt->mnt_parent;
	while (mnt && mnt->mnt_parent && mnt != mnt->mnt_parent &&
	       sus_mount_is_ours(mnt, id_min, prefix))
		mnt = mnt->mnt_parent;
	return mnt ? (int)mnt->mnt_id : 0;
}
