/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sus_mount_identity.c - identity records for sus_mount
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/mount.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/string.h>
#include <linux/smp.h>

#include "mount.h"
#include "susfs_log.h"
#include "sus_mount_identity.h"

#define SUS_MOUNT_DEVNAME_MAX 64
#define SUS_MOUNT_IDENT_MAX 1024

struct sus_mount_ident {
    dev_t s_dev;
    unsigned long root_ino;	/* 0 = free slot */
    bool devname_is_path;
    bool devname_truncated;
    char devname[SUS_MOUNT_DEVNAME_MAX];
};

static struct sus_mount_ident mount_ident[SUS_MOUNT_IDENT_MAX];
static int n_ident;
static DEFINE_SPINLOCK(ident_lock);
static atomic_t n_ident_full = ATOMIC_INIT(0);
static atomic_t n_ident_dropped_dev = ATOMIC_INIT(0);

/* Device paths are not source-path identities (issue #167). */
static bool sus_mount_devname_is_path(const char *name)
{
    return name && name[0] == '/' && strncmp(name, "/dev/", 5) != 0;
}

void sus_mount_ident_add(struct mount *r, int dbg)
{
    const char *devname = r->mnt_devname;
    struct dentry *root = r->mnt.mnt_root;
    unsigned long ino;
    unsigned long flags;
    int i, slot = -1;

    if (!root || !root->d_inode)
        return;
    ino = root->d_inode->i_ino;
    if (!ino)
        return;

    spin_lock_irqsave(&ident_lock, flags);
    for (i = 0; i < n_ident; i++) {
        if (mount_ident[i].s_dev == r->mnt.mnt_sb->s_dev &&
            mount_ident[i].root_ino == ino)
            goto out;
        if (!mount_ident[i].root_ino && slot < 0)
            slot = i;
    }
    if (slot < 0) {
        if (n_ident >= SUS_MOUNT_IDENT_MAX) {
            if (atomic_inc_return(&n_ident_full) == 1)
                pr_warn("sus_mount: identity table full (%d), %s is NOT recognised in other namespaces\n",
                        SUS_MOUNT_IDENT_MAX,
                        devname ? devname : "(no devname)");
            goto out;
        }
        slot = n_ident;
        smp_store_release(&n_ident, n_ident + 1);
    }
    {
        struct sus_mount_ident *e = &mount_ident[slot];

        e->s_dev = r->mnt.mnt_sb->s_dev;
        e->devname_is_path = sus_mount_devname_is_path(devname);
        if (e->devname_is_path) {
            e->devname_truncated = strlen(devname) >= sizeof(e->devname);
            strscpy(e->devname, devname, sizeof(e->devname));
        } else {
            e->devname[0] = '\0';
        }
        if (dbg)
            SUSFS_LOGI("sus_mount: ident[%d] s_dev=%u root_ino=%lu devname=%s\n",
                    slot, (unsigned int)e->s_dev, ino,
                    e->devname_is_path ? e->devname : "(not path-shaped)");
        smp_store_release(&e->root_ino, ino);
    }
out:
    spin_unlock_irqrestore(&ident_lock, flags);
}

bool sus_mount_ident_match(struct mount *r)
{
    int n = smp_load_acquire(&n_ident);
    const char *devname = r->mnt_devname;
    struct dentry *root = r->mnt.mnt_root;
    unsigned long ino = (root && root->d_inode) ? root->d_inode->i_ino : 0;
    int i;

    for (i = 0; i < n; i++) {
        const struct sus_mount_ident *e = &mount_ident[i];
        unsigned long e_ino = smp_load_acquire(&e->root_ino);

        if (!e_ino)
            continue;
        if (e_ino == ino && e->s_dev == r->mnt.mnt_sb->s_dev)
            return true;
        if (e->devname_is_path && devname &&
            (e->devname_truncated
                 ? !strncmp(e->devname, devname, sizeof(e->devname) - 1)
                 : !strcmp(e->devname, devname)))
            return true;
    }
    return false;
}

void sus_mount_ident_drop_dev(dev_t s_dev)
{
    unsigned long flags;
    int i;

    spin_lock_irqsave(&ident_lock, flags);
    for (i = 0; i < n_ident; i++) {
        if (smp_load_acquire(&mount_ident[i].root_ino) && mount_ident[i].s_dev == s_dev) {
            mount_ident[i].devname_is_path = false;
            mount_ident[i].devname[0] = '\0';
            smp_store_release(&mount_ident[i].root_ino, 0);
            atomic_inc(&n_ident_dropped_dev);
        }
    }
    spin_unlock_irqrestore(&ident_lock, flags);
}

void sus_mount_ident_reset(void)
{
    WRITE_ONCE(n_ident, 0);
}

int sus_mount_ident_live(void)
{
    unsigned long flags;
    int i, live = 0;

    spin_lock_irqsave(&ident_lock, flags);
    for (i = 0; i < READ_ONCE(n_ident); i++)
        if (smp_load_acquire(&mount_ident[i].root_ino))
            live++;
    spin_unlock_irqrestore(&ident_lock, flags);
    return live;
}

int sus_mount_ident_used(void)
{
    return READ_ONCE(n_ident);
}

int sus_mount_ident_full(void)
{
    return atomic_read(&n_ident_full);
}

int sus_mount_ident_dropped_dev(void)
{
    return atomic_read(&n_ident_dropped_dev);
}
