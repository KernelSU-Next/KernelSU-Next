/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __SUS_MOUNT_IDENTITY_LOGIC_H
#define __SUS_MOUNT_IDENTITY_LOGIC_H

#include <linux/types.h>

void sus_mount_ident_add_values(dev_t s_dev, unsigned long ino,
                                const char *devname, int dbg);
bool sus_mount_ident_match_values(dev_t s_dev, unsigned long ino,
                                  const char *devname);

#endif
