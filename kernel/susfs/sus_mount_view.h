/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sus_mount_view.h - value view shared by sus_mount's layers
 *
 * The view carries values, never kernel object pointers.  The kernel build
 * uses <linux/types.h>; the host-side view test uses fixed-width types.
 */

#ifndef __SUS_MOUNT_VIEW_H
#define __SUS_MOUNT_VIEW_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint32_t dev_t;
typedef uint32_t uid_t;
#endif

#define SUS_MOUNT_VIEW_DEVNAME_MAX 128
#define SUS_MOUNT_VIEW_PATH_MAX    512

enum sus_mount_context {
	SUS_MOUNT_CTX_SHOW,
	SUS_MOUNT_CTX_SCAN,
	SUS_MOUNT_CTX_RECORD,
};

enum sus_mount_source {
	SUS_MOUNT_SRC_UNKNOWN,
	SUS_MOUNT_SRC_DEVNAME,
	SUS_MOUNT_SRC_PATH,
};

enum sus_mount_show_action {
	SUS_MOUNT_SHOW_VISIBLE,
	SUS_MOUNT_SHOW_HIDE,
};

enum sus_mount_record_action {
	SUS_MOUNT_RECORD_SKIP,
	SUS_MOUNT_RECORD_TAKE,
};

enum sus_mount_valid {
	SUS_MOUNT_V_MNT_ID    = 1u << 0,
	SUS_MOUNT_V_PARENT    = 1u << 1,
	SUS_MOUNT_V_DEVID     = 1u << 2,
	SUS_MOUNT_V_DEVNAME   = 1u << 3,
	SUS_MOUNT_V_PATH      = 1u << 4,
	SUS_MOUNT_V_MNT_NS    = 1u << 5,
	SUS_MOUNT_V_USER_NS   = 1u << 6,
	SUS_MOUNT_V_IDMAP_NS  = 1u << 7,
	SUS_MOUNT_V_SU_DOMAIN = 1u << 8,
	SUS_MOUNT_V_CONTROL   = 1u << 9,
	SUS_MOUNT_V_ROOT_INO  = 1u << 10,
};

struct sus_mount_view {
	u32 valid_mask;
	enum sus_mount_context ctx;
	int mnt_id;
	int parent_id;
	dev_t s_dev;
	unsigned long root_ino;
	u32 mnt_ns_cookie;
	u32 user_ns_cookie;
	u32 idmap_cookie;
	char source_devname[SUS_MOUNT_VIEW_DEVNAME_MAX];
	u16 source_len;
	bool source_truncated;
	char mountpoint_path[SUS_MOUNT_VIEW_PATH_MAX];
	u16 path_len;
	bool path_truncated;
	bool path_unavailable;
	bool prefix_matched;
	enum sus_mount_source source;
	bool caller_is_su_domain;
	bool control_visible;
};

static inline bool sus_mount_view_has(const struct sus_mount_view *v, u32 bit)
{
	return v && (v->valid_mask & bit) == bit;
}

static inline enum sus_mount_record_action
sus_mount_record_decide(const struct sus_mount_view *v, bool devname_ours,
			bool path_ours)
{
	(void)v;
	return (devname_ours || path_ours) ? SUS_MOUNT_RECORD_TAKE
					   : SUS_MOUNT_RECORD_SKIP;
}

static inline enum sus_mount_show_action
sus_mount_show_decide(const struct sus_mount_view *v, bool is_ours, bool su)
{
	(void)v;
	if (!is_ours || su)
		return SUS_MOUNT_SHOW_VISIBLE;
	return SUS_MOUNT_SHOW_HIDE;
}

static inline bool sus_mount_view_ctx_valid(enum sus_mount_context ctx)
{
	return ctx == SUS_MOUNT_CTX_SHOW || ctx == SUS_MOUNT_CTX_SCAN ||
	       ctx == SUS_MOUNT_CTX_RECORD;
}

/* Fields that each context is allowed to fill. */
static inline u32 sus_mount_view_required(enum sus_mount_context ctx)
{
	switch (ctx) {
	case SUS_MOUNT_CTX_SHOW:
		return SUS_MOUNT_V_MNT_ID | SUS_MOUNT_V_PARENT |
		       SUS_MOUNT_V_DEVID | SUS_MOUNT_V_ROOT_INO | SUS_MOUNT_V_DEVNAME |
		       SUS_MOUNT_V_MNT_NS | SUS_MOUNT_V_USER_NS |
		       SUS_MOUNT_V_IDMAP_NS | SUS_MOUNT_V_SU_DOMAIN |
		       SUS_MOUNT_V_CONTROL;
	case SUS_MOUNT_CTX_SCAN:
	case SUS_MOUNT_CTX_RECORD:
		return SUS_MOUNT_V_MNT_ID | SUS_MOUNT_V_PARENT |
		       SUS_MOUNT_V_DEVID | SUS_MOUNT_V_ROOT_INO | SUS_MOUNT_V_DEVNAME |
		       SUS_MOUNT_V_PATH | SUS_MOUNT_V_MNT_NS |
		       SUS_MOUNT_V_USER_NS | SUS_MOUNT_V_IDMAP_NS;
	default:
		return 0;
	}
}

static inline void sus_mount_view_set_path(struct sus_mount_view *out,
					   const char *path, size_t len)
{
	size_t n;

	if (!out || !path)
		return;
	n = len < sizeof(out->mountpoint_path) - 1 ?
		len : sizeof(out->mountpoint_path) - 1;
	memcpy(out->mountpoint_path, path, n);
	out->mountpoint_path[n] = 0;
	out->path_len = (u16)n;
	out->path_truncated = len > n;
	out->path_unavailable = false;
	out->source = SUS_MOUNT_SRC_PATH;
	out->valid_mask |= SUS_MOUNT_V_PATH;
}

#ifdef __KERNEL__
struct mount;

struct sus_mount_walk;

struct sus_mount_walk *
sus_mount_walk_begin(struct mount *start, enum sus_mount_context ctx);
void sus_mount_view_from_mount(struct sus_mount_view *out, struct mount *m,
			       enum sus_mount_context ctx);
bool sus_mount_walk_next(struct sus_mount_walk *walk,
			 struct sus_mount_view *out, int *err);
void sus_mount_walk_end(struct sus_mount_walk *walk);
#endif

#endif /* __SUS_MOUNT_VIEW_H */
