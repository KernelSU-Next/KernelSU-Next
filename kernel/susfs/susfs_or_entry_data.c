/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_or_entry_data.c - open_redirect name-layer state (D layer)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include "susfs_or_entry_data.h"

struct susfs_or_symbols susfs_or_syms;

bool susfs_or_entry_ready;
atomic_t susfs_or_entry_hits = ATOMIC_INIT(0);

/* On by default: this IS the forward direction now (the vfs_open probe is gone from
 * susfs_open_redirect.c).  Off means the layer is not armed at all - see
 * susfs_or_entry_init() - so a rule is refused rather than accepted and inert. */
bool susfs_or_entry_param = true;
module_param_named(or_entry, susfs_or_entry_param, bool, 0600);
