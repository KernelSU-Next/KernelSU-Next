/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_opath_data.c - O_PATH layer counters and switches (D layer)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/atomic.h>

#include "susfs_opath_data.h"

/* What the layer did, not what it is configured to do: a wrapper that never runs and one that
 * runs and matches nothing look identical from the outside, and only these separate them. */
atomic_t n_opath_seen = ATOMIC_INIT(0);	/* wrapper calls, hidden or not */
atomic_t n_opath_opath = ATOMIC_INIT(0);	/* of those, the O_PATH ones */
atomic_t n_opath_hidden = ATOMIC_INIT(0);	/* of those, sus_path said hidden */
atomic_t n_opath_deny = ATOMIC_INIT(0);	/* of those, the fd was taken away */
atomic_t n_opath_err = ATOMIC_INIT(0);	/* lookups that failed, or an fd not ours */

int opath_test;
module_param_named(opath_test, opath_test, int, 0644);

int opath_dump;
module_param_named(opath_dump, opath_dump, int, 0644);

/* Set by susfs_opath_exit() before it touches anything; read by the arm path.  There is no
 * lock around it, and that is deliberate - see the note in susfs_opath_exit(). */
bool opath_exiting;

