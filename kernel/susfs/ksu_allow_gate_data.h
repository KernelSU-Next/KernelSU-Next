/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ksu_allow_gate_data.h - KernelSU allow-predicate state (D layer)
 */

#ifndef __KSU_ALLOW_GATE_DATA_H
#define __KSU_ALLOW_GATE_DATA_H

#include <linux/types.h>

/* 0 = no source resolved (strict fallback), 1 = KernelSU's predicate. */
extern int susfs_ksu_allow_kind;

/* Whether the control nodes actually carry the ksu_file label. */
extern bool susfs_control_relabelled;

#endif /* __KSU_ALLOW_GATE_DATA_H */
