/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_opath.h - the O_PATH layer: its control entry points only.
 *
 * The hidden decision this layer post-checks with belongs to sus_path and is declared
 * there (susfs.h), next to sus_path_init()/sus_path_lsm_active(): a header for one layer
 * must not pretend to own another layer's API.
 */

#ifndef __SUSFS_OPATH_H
#define __SUSFS_OPATH_H

/* Both are read at INIT, like fail_layer in susfs_main.c, so they belong on the insmod
 * command line (`insmod susfs_guard_lkm.ko opath_test=2`); a later write to
 * /sys/module/susfs_guard_lkm/parameters/ changes the variable and nothing else.
 *
 * opath_test: 1-based index of the ONE table entry to install, 0 = all of them.  One entry
 * at a time is what makes a first bring-up diagnosable: a wrapper that faults shows up as a
 * failure of that entry, with the other three still answering the kernel's own way.
 * opath_dump: print the diagnostics (each entry's current value, the plain and .cfi_jt
 * symbols, whether the wrapper starts with 'bti c') and install NOTHING - the readings a
 * step has to be judged against have to come from the device, not from the build host. */
extern int opath_test;
extern int opath_dump;

int susfs_opath_init(void);
void susfs_opath_exit(void);

/* The layer's control node: reading it is the only live view of the counters, and
 * "armed" without a hit count cannot tell a wrapper that never runs from one that runs and
 * matches nothing.  Registered in susfs_main.c's self-hide list like every other node of
 * this module (0777 plus sus_path's LSM layer answering ENOENT, see susfs_main.c). */
#define SUSFS_OPATH_NODE "/proc/susfs_opath"

#endif /* __SUSFS_OPATH_H */
