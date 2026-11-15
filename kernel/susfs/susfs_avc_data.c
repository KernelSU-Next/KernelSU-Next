/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_avc_data.c - avc spoof state (D layer)
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mutex.h>

#include "susfs_avc_data.h"

u32 susfs_avc_su_sid;
u32 susfs_avc_priv_app_sid;
bool susfs_avc_registered;
bool susfs_avc_exiting;

/* Serialises the arm/disarm pair.  register_kprobe() resolves .symbol_name only while
 * .addr is NULL and answers -EINVAL once both are set, so a *successful* registration
 * by another task makes this task's register_kprobe() fail - and the failure branch
 * clears .addr.  Without this lock that clears the address of a probe that is LIVE:
 * unregister_kprobes() only runs a probe's bottom half `if (kps[i]->addr)`, so the
 * breakpoint then survives the unload with its handler pointing into freed module
 * text.  Two independent entries reach avc_register() (/proc/susfs_avc_spoof and the
 * supercall, which runs in each caller's task_work). */
struct mutex susfs_avc_ctl_lock;
atomic_t susfs_avc_enter_count = ATOMIC_INIT(0);
atomic_t susfs_avc_hit_count = ATOMIC_INIT(0);

void susfs_avc_data_init(u32 su_sid, u32 priv_app_sid)
{
	mutex_init(&susfs_avc_ctl_lock);
	susfs_avc_su_sid = su_sid;
	susfs_avc_priv_app_sid = priv_app_sid;
}
