/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_avc_policy.h - avc spoof decision and lifecycle (L layer)
 */

#ifndef __SUSFS_AVC_POLICY_H
#define __SUSFS_AVC_POLICY_H

#include <linux/types.h>

struct susfs_hook;

/* consumer for the slow_avc_audit hook: rewrites ssid/tsid in place. */
int susfs_avc_pre_consumer(void *ctx);

/* Install the hook, refusing after the exit path closed the door.  The caller
 * owns the hook object (F layer) and must keep it alive for the unregister. */
int susfs_avc_register(struct susfs_hook *hook);

/* Disarm.  The exit variant also refuses further registrations, in one critical
 * section with the disarm.  The caller passes its hook object (F layer) so this
 * layer never reaches back into it. */
void susfs_avc_unregister(struct susfs_hook *hook);
void susfs_avc_unregister_for_exit(struct susfs_hook *hook);

#endif /* __SUSFS_AVC_POLICY_H */
