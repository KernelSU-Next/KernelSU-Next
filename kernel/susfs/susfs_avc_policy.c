/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_avc_policy.c - avc spoof decision and lifecycle (L layer)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/atomic.h>

#include "susfs_avc_data.h"
#include "susfs_avc_policy.h"
#include "susfs_hooks.h"
#include "susfs_log.h"

int susfs_avc_pre_consumer(void *ctx)
{
	struct susfs_avc_ctx *c = ctx;
	bool hit = false;

	atomic_inc(&susfs_avc_enter_count);
	if (!susfs_avc_su_sid || !susfs_avc_priv_app_sid)
		return 0;
	if (c->ssid == susfs_avc_su_sid) {
		c->ssid = susfs_avc_priv_app_sid;
		hit = true;
	}
	if (c->tsid == susfs_avc_su_sid) {
		c->tsid = susfs_avc_priv_app_sid;
		hit = true;
	}
	if (hit)
		atomic_inc(&susfs_avc_hit_count);
	return 0;
}

int susfs_avc_register(struct susfs_hook *hook)
{
	int rc;

	mutex_lock(&susfs_avc_ctl_lock);
	if (susfs_avc_exiting) {
		mutex_unlock(&susfs_avc_ctl_lock);
		return -ENODEV;
	}
	if (susfs_avc_registered) {
		mutex_unlock(&susfs_avc_ctl_lock);
		return 0;
	}
	/* The handler would bail out on every call, so report it here instead of arming a
	 * probe that can never spoof anything. */
	if (!susfs_avc_su_sid || !susfs_avc_priv_app_sid) {
		mutex_unlock(&susfs_avc_ctl_lock);
		pr_warn("avc_spoof: su_sid=%u priv_app_sid=%u - both contexts must resolve against this device's policy (override with avc_su_ctx=/avc_priv_app_ctx=)\n",
			susfs_avc_su_sid, susfs_avc_priv_app_sid);
		return -EOPNOTSUPP;
	}
	rc = susfs_hook_register(hook);
	if (rc < 0) {
		mutex_unlock(&susfs_avc_ctl_lock);
		return rc;
	}
	susfs_avc_registered = true;
	mutex_unlock(&susfs_avc_ctl_lock);
	if (rc == SUSFS_HOOK_FALLBACK)
		SUSFS_LOGI("susfs_avc_spoof: slow_avc_audit installed on fallback backend\n");
	else
		SUSFS_LOGI("susfs_avc_spoof: slow_avc_audit installed inline\n");
	return 0;
}

/* Caller must hold susfs_avc_ctl_lock.  Kept separate from susfs_avc_unregister()
 * because the exit path has to do the disarm and "refuse further registration" in
 * ONE critical section - something the /proc write path (which also disarms, but
 * must stay re-enablable) must not do. */
static void susfs_avc_unregister_locked(struct susfs_hook *hook)
{
	if (!susfs_avc_registered)
		return;
	susfs_hook_unregister(hook);
	susfs_avc_registered = false;
	/* Reported only for a real disarm: writing '0' when nothing was armed (or a
	 * second disable) used to be silent and must stay silent. */
	SUSFS_LOGI("susfs_avc_spoof: hook removed\n");
}

void susfs_avc_unregister(struct susfs_hook *hook)
{
	mutex_lock(&susfs_avc_ctl_lock);
	susfs_avc_unregister_locked(hook);
	mutex_unlock(&susfs_avc_ctl_lock);
}

void susfs_avc_unregister_for_exit(struct susfs_hook *hook)
{
	mutex_lock(&susfs_avc_ctl_lock);
	susfs_avc_exiting = true;
	susfs_avc_unregister_locked(hook);
	mutex_unlock(&susfs_avc_ctl_lock);
}
