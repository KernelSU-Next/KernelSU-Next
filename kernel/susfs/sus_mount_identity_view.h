/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __SUS_MOUNT_IDENTITY_VIEW_H
#define __SUS_MOUNT_IDENTITY_VIEW_H

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif
#include "sus_mount_identity_values.h"

/* L: device paths are not source-path identities (issue #167). */
static inline bool sus_mount_ident_path_core(const char *name)
{
    return name && name[0] == '/' && strncmp(name, "/dev/", 5) != 0;
}

/* The acquired inode publication value is supplied separately. Do not read
 * root_ino again or strengthen the original lockless-reader contract here.
 */
static inline bool sus_mount_ident_match_core(const struct sus_mount_ident *e,
        unsigned long published_ino, sus_mount_ident_dev_t s_dev,
        unsigned long ino, const char *devname)
{
    if (!published_ino)
        return false;
    if (published_ino == ino && e->s_dev == s_dev)
        return true;
    return e->devname_is_path && devname &&
           (e->devname_truncated
                ? !strncmp(e->devname, devname, sizeof(e->devname) - 1)
                : !strcmp(e->devname, devname));
}

static inline bool sus_mount_id_owned_core(unsigned int id, unsigned int id_min)
{
    return id >= id_min;
}

#endif
