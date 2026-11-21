/* SPDX-License-Identifier: GPL-2.0-only */
/* L: record insertion and identity decisions on values supplied by P. */
#include <linux/kernel.h>
#include <linux/smp.h>
#include "susfs_log.h"
#include "sus_mount_identity_data.h"
#include "sus_mount_identity_logic.h"
#include "sus_mount_identity_view.h"

void sus_mount_ident_add_values(dev_t s_dev, unsigned long ino,
                                const char *devname, int dbg)
{
    unsigned long flags;
    int i, slot = -1;

    if (!ino)
        return;

    spin_lock_irqsave(&sus_mount_ident_lock, flags);
    for (i = 0; i < sus_mount_n_ident; i++) {
        if (sus_mount_idents[i].s_dev == s_dev &&
            sus_mount_idents[i].root_ino == ino)
            goto out;
        if (!sus_mount_idents[i].root_ino && slot < 0)
            slot = i;
    }
    if (slot < 0) {
        if (sus_mount_n_ident >= SUS_MOUNT_IDENT_MAX) {
            if ((unsigned int)atomic_inc_return(&sus_mount_ident_full_count) == 1u)
                pr_warn("sus_mount: identity table full (%d), %s is NOT recognised in other namespaces\n",
                        SUS_MOUNT_IDENT_MAX,
                        devname ? devname : "(no devname)");
            goto out;
        }
        slot = sus_mount_n_ident;
        smp_store_release(&sus_mount_n_ident, sus_mount_n_ident + 1);
    }
    {
        struct sus_mount_ident *e = &sus_mount_idents[slot];

        e->s_dev = s_dev;
        e->devname_is_path = sus_mount_ident_path_core(devname);
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
    spin_unlock_irqrestore(&sus_mount_ident_lock, flags);
}

bool sus_mount_ident_match_values(dev_t s_dev, unsigned long ino,
                                  const char *devname)
{
    int n = smp_load_acquire(&sus_mount_n_ident);
    int i;

    for (i = 0; i < n; i++) {
        const struct sus_mount_ident *e = &sus_mount_idents[i];
        unsigned long e_ino = smp_load_acquire(&e->root_ino);

        if (!e_ino)
            continue;
        if (sus_mount_ident_match_core(e, e_ino, s_dev, ino, devname))
            return true;
    }
    return false;
}

