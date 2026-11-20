/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ksu_umount_gate_data.c - KernelSU umount predicate state (D layer)
 */

#include <linux/kernel.h>
#include <linux/types.h>

#include "ksu_umount_gate_data.h"

bool susfs_ksu_umount_gate_off = true;
