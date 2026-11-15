/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SUSFS_AVC_SPOOF_H
#define __SUSFS_AVC_SPOOF_H

#include <linux/compiler_types.h>

int susfs_avc_spoof_init(void);
void susfs_avc_spoof_exit(void);

void susfs_avc_spoof_supercall(void __user **arg);

#endif /* __SUSFS_AVC_SPOOF_H */
