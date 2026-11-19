/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SUSFS_SUPERCALL_H
#define __SUSFS_SUPERCALL_H

#include <linux/types.h>
#include "susfs_abi.h"		/* enum susfs_feature lives with the ABI */

int susfs_supercall_init(void);
void susfs_supercall_exit(void);
/* True when the sys_call_table hook could not arm and the supercall is delivered
 * through the kprobe fallback (see susfs_supercall_init()). */
bool susfs_supercall_kprobe_active(void);

/* Feature registry for the reboot(2) supercall.  A layer arms its feature only after its init
 * returned 0, so a non-fatal layer that failed is neither advertised by SHOW_ENABLED_FEATURES
 * nor allowed to have its commands hijacked (userspace then keeps its ERR_CMD_NOT_SUPPORTED
 * sentinel).  The reboot entry reads the bitmap with test_bit(); the arm happens once, in
 * process context, before the module can serve a command. */
void susfs_supercall_arm_feature(enum susfs_feature feature);
bool susfs_supercall_feature_armed(enum susfs_feature feature);

#endif /* __SUSFS_SUPERCALL_H */
