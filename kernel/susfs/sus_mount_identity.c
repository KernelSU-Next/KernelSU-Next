/* SPDX-License-Identifier: GPL-2.0-only */
/* P: extract mount identity values; record storage and matching live below. */
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/mount.h>
#include "mount.h"
#include "sus_mount_identity.h"
#include "sus_mount_identity_logic.h"

void sus_mount_ident_add(struct mount *r, int dbg)
{
    struct dentry *root = r->mnt.mnt_root;
    const char *devname = r->mnt_devname;
    unsigned long ino;

    if (!root || !root->d_inode)
        return;
    ino = root->d_inode->i_ino;
    if (!ino)
        return;
    sus_mount_ident_add_values(r->mnt.mnt_sb->s_dev, ino,
                              devname, dbg);
}

bool sus_mount_ident_match(struct mount *r)
{
    struct dentry *root = r->mnt.mnt_root;
    unsigned long ino = (root && root->d_inode) ? root->d_inode->i_ino : 0;

    return sus_mount_ident_match_values(r->mnt.mnt_sb->s_dev, ino,
                                       r->mnt_devname);
}
