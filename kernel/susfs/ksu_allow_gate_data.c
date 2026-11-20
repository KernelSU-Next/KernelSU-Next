/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ksu_allow_gate_data.c - KernelSU allow-predicate state (D layer)
 */

#include <linux/kernel.h>
#include <linux/types.h>

#include "ksu_allow_gate_data.h"

/* 0 = no source resolved (strict fallback), 1 = KernelSU's predicate.  See the
 * header. */
int susfs_ksu_allow_kind;

/* Whether the control nodes actually carry the ksu_file label, i.e. whether the
 * KernelSU set may be let through the gate at all.  Written once by
 * susfs_relabel_control_nodes(), read by every caller; see ksu_allow_gate.h for
 * why an unrelabelled node has to stay root-only. */
bool susfs_control_relabelled;
