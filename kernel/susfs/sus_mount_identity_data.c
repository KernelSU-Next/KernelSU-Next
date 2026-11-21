/* SPDX-License-Identifier: GPL-2.0-only */
/* D: identity storage, publication, counters and maintenance. */
#include <linux/kernel.h>
#include <linux/smp.h>
#include "sus_mount_identity_data.h"

struct sus_mount_ident sus_mount_idents[SUS_MOUNT_IDENT_MAX];
int sus_mount_n_ident;
DEFINE_SPINLOCK(sus_mount_ident_lock);
atomic_t sus_mount_ident_full_count = ATOMIC_INIT(0);
atomic_t sus_mount_ident_drop_count = ATOMIC_INIT(0);

void sus_mount_ident_drop_dev(dev_t s_dev)
{
    unsigned long flags;
    int i;

    spin_lock_irqsave(&sus_mount_ident_lock, flags);
    for (i = 0; i < sus_mount_n_ident; i++) {
        if (smp_load_acquire(&sus_mount_idents[i].root_ino) && sus_mount_idents[i].s_dev == s_dev) {
            sus_mount_idents[i].devname_is_path = false;
            sus_mount_idents[i].devname[0] = '\0';
            smp_store_release(&sus_mount_idents[i].root_ino, 0);
            atomic_inc(&sus_mount_ident_drop_count);
        }
    }
    spin_unlock_irqrestore(&sus_mount_ident_lock, flags);
}

void sus_mount_ident_reset(void)
{
    WRITE_ONCE(sus_mount_n_ident, 0);
}

int sus_mount_ident_live(void)
{
    unsigned long flags;
    int i, live = 0;

    spin_lock_irqsave(&sus_mount_ident_lock, flags);
    for (i = 0; i < READ_ONCE(sus_mount_n_ident); i++)
        if (smp_load_acquire(&sus_mount_idents[i].root_ino))
            live++;
    spin_unlock_irqrestore(&sus_mount_ident_lock, flags);
    return live;
}

int sus_mount_ident_used(void)
{
    return READ_ONCE(sus_mount_n_ident);
}

int sus_mount_ident_full(void)
{
    return atomic_read(&sus_mount_ident_full_count);
}

int sus_mount_ident_dropped_dev(void)
{
    return atomic_read(&sus_mount_ident_drop_count);
}
