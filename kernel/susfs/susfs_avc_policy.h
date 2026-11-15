/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_avc_policy.h - avc spoof decision and lifecycle (L layer)
 */

#ifndef __SUSFS_AVC_POLICY_H
#define __SUSFS_AVC_POLICY_H

#include <linux/types.h>

struct susfs_hook;

struct susfs_avc_status {
	bool enabled;
	u32 su_sid;
	u32 priv_app_sid;
	int enter;
	int hits;
};

/* Called at boot before exposing the control endpoints. */
void susfs_avc_policy_init(u32 su_sid, u32 priv_app_sid);
void susfs_avc_status_read(struct susfs_avc_status *status);

/* consumer for the slow_avc_audit hook: rewrites ssid/tsid in place. */
int susfs_avc_pre_consumer(void *ctx);

/* Hook installation and reported enabled state have one lock and one source
 * of truth. F must not skip this call based on an earlier status snapshot. */
int susfs_avc_set_enabled(struct susfs_hook *hook, bool enabled);

/* Disarm.  The exit variant also refuses further registrations, in one critical
 * section with the disarm.  The caller passes its hook object (F layer) so this
 * layer never reaches back into it. */
void susfs_avc_unregister_for_exit(struct susfs_hook *hook);

#endif /* __SUSFS_AVC_POLICY_H */
