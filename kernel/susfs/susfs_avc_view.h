/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __SUSFS_AVC_VIEW_H
#define __SUSFS_AVC_VIEW_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t u32;
#endif

/* Value-only decision: no kernel audit object or mutable control state. */
struct susfs_avc_view {
	u32 ssid;
	u32 tsid;
	u32 su_sid;
	u32 priv_app_sid;
};

static inline bool susfs_avc_decide_core(struct susfs_avc_view *v)
{
	bool hit = false;

	if (!v || !v->su_sid || !v->priv_app_sid)
		return false;
	if (v->ssid == v->su_sid) {
		v->ssid = v->priv_app_sid;
		hit = true;
	}
	if (v->tsid == v->su_sid) {
		v->tsid = v->priv_app_sid;
		hit = true;
	}
	return hit;
}

#endif /* __SUSFS_AVC_VIEW_H */
