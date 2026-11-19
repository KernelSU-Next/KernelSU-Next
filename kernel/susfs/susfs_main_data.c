/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_main_data.c - module entry data (D layer)
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>

#include "susfs_control.h"
#include "sus_path.h"
#include "susfs_opath.h"
#include "sus_map.h"
#include "sus_mount.h"
#include "susfs_hide_syms.h"
#include "susfs_core.h"
#include "susfs_hook_map.h"
#include "susfs_main_data.h"

/* Default-0 capability map.  The user-space loader writes the probed bits and
 * ready last; ksud leaves ready 0, so every slot keeps the current fallback. */
struct susfs_hook_map susfs_hook_map
	__section(SUSFS_HOOK_MAP_SECTION) __aligned(8) __used = { };

/* The /proc/susfs_* control nodes are created by default and are 0777 on purpose.
 * Measured on device, 0600 plus sus_path hiding does not work: `ls -l /proc/susfs_kstat`
 * -> ENOENT (the inode_getattr hook fired) but `cat` -> Permission denied (it did not), with
 * sus_path's perm counter stuck at 0 - inode_permission() runs the DAC check BEFORE
 * security_inode_permission(), so a 0600 root-owned node fails DAC first, the LSM hook is
 * never reached, and EACCES advertises that the node exists.  General to sus_path's path
 * layer: it can only turn an ENOENT-shaped answer out of files DAC would have ALLOWED.
 * Hence 0777: DAC passes, the LSM layer is the only thing that answers (ENOENT for every
 * non-root caller).
 * The nodes exist only when the layer that hides them is installed
 * (susfs_control_node_allowed()), so an unprotected world-writable node cannot happen, and
 * root-only access is enforced by the handlers' uid checks, not by the mode.  expose_proc=0
 * removes the nodes entirely. */
bool susfs_expose_proc = true;
module_param_named(expose_proc, susfs_expose_proc, bool, 0600);

const char *const susfs_self_hide_paths[] = {
    "/proc/susfs_kstat",
    "/proc/susfs_open_redirect",
    "/proc/susfs_enable_log",
    "/proc/susfs_avc_spoof",
    SUSFS_HIDE_MODULES_NODE,
    SUSFS_HIDE_MOUNTS_NODE,
    /* sus_path's own interface (the rule listing): it goes through the same table it
     * prints, so a broken rule table is visible in the very view that reports it. */
    SUSFS_PATH_NODE,
    /* The O_PATH layer's node: it reports the four sys_call_table/compat_sys_call_table
     * entries and the counters that say whether a wrapper ever ran. */
    SUSFS_OPATH_NODE,
    SUSFS_MAP_NODE,
    /* The module's own sysfs directory is NOT here: it belongs to hide_module
     * (susfs_hide_syms.c), which adds and drops that rule at runtime. */
};
const unsigned int susfs_self_hide_paths_count = ARRAY_SIZE(susfs_self_hide_paths);
