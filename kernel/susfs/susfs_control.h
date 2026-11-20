/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SUSFS_CONTROL_H
#define __SUSFS_CONTROL_H

#include <linux/types.h>
#include "sus_path.h"
#include "susfs_main_data.h"	/* susfs_expose_proc (D layer) */

/* Whether a 0777 /proc/susfs_* control node may be created.  Two independent conditions, neither optional: susfs_expose_proc
 * (the operator opted in) and sus_path_lsm_active() (with 0777 DAC passes every caller, so only the LSM layer can still
 * answer ENOENT for an app).  This gates node CREATION ONLY, never hook registration: an earlier revision returned early from
 * the feature inits on !susfs_expose_proc, which silently disabled sus_kstat's tracepoint and kretprobe. */
static inline bool susfs_control_node_allowed(void)
{
	return susfs_expose_proc && sus_path_lsm_active();
}

#endif /* __SUSFS_CONTROL_H */
