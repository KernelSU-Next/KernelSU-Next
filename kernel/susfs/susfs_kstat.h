/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SUSFS_KSTAT_H
#define __SUSFS_KSTAT_H

#include <linux/compiler_types.h>

int susfs_kstat_init(void);
void susfs_kstat_exit(void);

void susfs_kstat_supercall(unsigned int cmd, void __user **arg);

#endif /* __SUSFS_KSTAT_H */
