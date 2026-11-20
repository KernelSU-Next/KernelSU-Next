/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ksu_umount_gate_data.h - KernelSU umount predicate state (D layer)
 */

#ifndef __KSU_UMOUNT_GATE_DATA_H
#define __KSU_UMOUNT_GATE_DATA_H

#include <linux/types.h>

/* Fail-safe default: the pre-#34 answer, until the init proves that KernelSU's
 * symbol exists. */
extern bool susfs_ksu_umount_gate_off;

#endif /* __KSU_UMOUNT_GATE_DATA_H */
