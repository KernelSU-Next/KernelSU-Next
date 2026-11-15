/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_avc_data.h - avc spoof state (D layer)
 *
 * The two contexts, the resolved sids, the switch and the serialisation of the
 * arm/disarm pair.  Nothing here decides anything; the policy layer reads it.
 */

#ifndef __SUSFS_AVC_DATA_H
#define __SUSFS_AVC_DATA_H

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/atomic.h>

#define SUSFS_AVC_CTX_LEN 128

extern char susfs_avc_su_ctx[SUSFS_AVC_CTX_LEN];
extern char susfs_avc_priv_app_ctx[SUSFS_AVC_CTX_LEN];

extern u32 susfs_avc_su_sid;
extern u32 susfs_avc_priv_app_sid;
extern bool susfs_avc_spoof_enabled;
extern bool susfs_avc_registered;
extern bool susfs_avc_exiting;

extern struct mutex susfs_avc_ctl_lock;
extern atomic_t susfs_avc_enter_count;
extern atomic_t susfs_avc_hit_count;

void susfs_avc_data_init(void);

#endif /* __SUSFS_AVC_DATA_H */
