/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SPOOF_CMDLINE_H
#define __SPOOF_CMDLINE_H

#include <linux/compiler_types.h>

int susfs_spoof_cmdline_init(void);
void susfs_spoof_cmdline_exit(void);

void susfs_spoof_cmdline_supercall(void __user **arg);

#endif /* __SPOOF_CMDLINE_H */
