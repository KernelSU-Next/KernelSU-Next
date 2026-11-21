/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __SUS_MOUNT_IDENTITY_VALUES_H
#define __SUS_MOUNT_IDENTITY_VALUES_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef dev_t sus_mount_ident_dev_t;
#else
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t sus_mount_ident_dev_t;
#endif

#define SUS_MOUNT_DEVNAME_MAX 64
#define SUS_MOUNT_IDENT_MAX 1024

/* D value record. root_ino == 0 marks a free slot. */
struct sus_mount_ident {
    sus_mount_ident_dev_t s_dev;
    unsigned long root_ino;
    bool devname_is_path;
    bool devname_truncated;
    char devname[SUS_MOUNT_DEVNAME_MAX];
};

#endif
