/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SUSFS_UNAME_H
#define __SUSFS_UNAME_H

#include <linux/compiler_types.h>

int susfs_uname_init(void);
void susfs_uname_exit(void);

void susfs_uname_supercall(void __user **arg);

#endif /* __SUSFS_UNAME_H */
