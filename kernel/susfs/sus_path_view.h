/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sus_path_view.h - value-only view for the sus_path decision layer
 *
 * F/P fill the view; L reads only these values.  The two faces are kept
 * explicit: the inode face knows the owner, the dirent face does not.
 */

#ifndef __SUS_PATH_VIEW_H
#define __SUS_PATH_VIEW_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint32_t uid_t;
#endif

#define SUS_PATH_VIEW_NAME_MAX 256

enum sus_path_face {
	SUS_PATH_FACE_INODE,
	SUS_PATH_FACE_DIRENT,
};

enum sus_path_decision {
	SUS_PATH_PASS,
	SUS_PATH_HIDE,
};

struct sus_path_view {
	enum sus_path_face face;

	u64 ino;
	u64 s_dev;
	uid_t i_uid;
	u32 sb_magic;
	char name[SUS_PATH_VIEW_NAME_MAX];

	bool pending;
	bool user_rules_present;
	bool rule_matched;
	bool self_protect;
	bool user_rule;

	bool is_resolver;
	uid_t caller_uid;
	bool caller_is_app;
	bool caller_is_su;
	bool hide_from_apps;
	bool control_visible;
};

static inline enum sus_path_decision
sus_path_decide_core(const struct sus_path_view *v)
{
	if (!v || v->is_resolver)
		return SUS_PATH_PASS;
	if (!v->rule_matched)
		return SUS_PATH_PASS;

	if (v->self_protect && !v->control_visible)
		return SUS_PATH_HIDE;

	if (!v->user_rule)
		return SUS_PATH_PASS;

	if (!v->hide_from_apps)
		return SUS_PATH_HIDE;

	if (v->face == SUS_PATH_FACE_INODE)
		return (v->caller_is_app && v->caller_uid != v->i_uid)
			? SUS_PATH_HIDE : SUS_PATH_PASS;

	/* The dirent filter has no inode, so it cannot apply the owner exception. */
	return v->caller_is_app ? SUS_PATH_HIDE : SUS_PATH_PASS;
}

#endif /* __SUS_PATH_VIEW_H */
