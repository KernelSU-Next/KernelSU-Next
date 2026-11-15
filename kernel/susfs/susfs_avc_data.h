/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_avc_data.h - avc spoof state (D layer)
 *
 * Resolved sids, installation state and arm/disarm serialisation.
 * Only the policy layer accesses these facts; F consumes a status snapshot.
 */

#ifndef __SUSFS_AVC_DATA_H
#define __SUSFS_AVC_DATA_H

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/atomic.h>

extern u32 susfs_avc_su_sid;
extern u32 susfs_avc_priv_app_sid;
extern bool susfs_avc_registered;
extern bool susfs_avc_exiting;

extern struct mutex susfs_avc_ctl_lock;
extern atomic_t susfs_avc_enter_count;
extern atomic_t susfs_avc_hit_count;

void susfs_avc_data_init(u32 su_sid, u32 priv_app_sid);

#endif /* __SUSFS_AVC_DATA_H */
