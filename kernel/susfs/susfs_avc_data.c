/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_avc_data.c - avc spoof state (D layer)
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mutex.h>

#include "susfs_avc_data.h"

/* module_param overrides for the two domains: the default su domain is the SukiSU
 * variant ("ksu"), stock KernelSU uses "su".  The sid is resolved at init time via
 * security_secctx_to_secid(). */
char susfs_avc_su_ctx[SUSFS_AVC_CTX_LEN] = "u:r:ksu:s0";
char susfs_avc_priv_app_ctx[SUSFS_AVC_CTX_LEN] = "u:r:priv_app:s0:c512,c768";

u32 susfs_avc_su_sid;
u32 susfs_avc_priv_app_sid;
bool susfs_avc_spoof_enabled;
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

void susfs_avc_data_init(void)
{
	mutex_init(&susfs_avc_ctl_lock);
}
