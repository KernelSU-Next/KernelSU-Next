/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_create_view.h - value-only view for the create gate
 *
 * The view is filled by F/P; the decision is pure and reads only the view.
 * It is not wired into the syscall wrappers yet; this file is the contract.
 */

#ifndef __SUSFS_CREATE_VIEW_H
#define __SUSFS_CREATE_VIEW_H

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

#define SUSFS_CREATE_NAME_MAX 256
#define SUSFS_CREATE_PATH_MAX 512

enum susfs_create_kind {
	SUSFS_CREATE_VIEW_MKDIRAT,
	SUSFS_CREATE_VIEW_MKNODAT,
	SUSFS_CREATE_VIEW_SYMLINKAT,
	SUSFS_CREATE_VIEW_LINKAT,
};

enum susfs_create_state {
	SUSFS_CREATE_ST_ABSENT,
	SUSFS_CREATE_ST_EXISTS,
	SUSFS_CREATE_ST_NOT_NAME,
	SUSFS_CREATE_ST_ERROR,
};

enum susfs_create_action {
	SUSFS_CREATE_PASS,
	SUSFS_CREATE_HIDE,
	SUSFS_CREATE_HIDE_OLD,
	SUSFS_CREATE_HIDE_NEW,
	SUSFS_CREATE_NOT_NAME,
	SUSFS_CREATE_ERROR,
};

struct susfs_create_view {
	enum susfs_create_kind kind;
	enum susfs_create_state state;

	u64 ino;
	u64 s_dev;
	uid_t i_uid;
	u32 sb_magic;

	char name[SUSFS_CREATE_NAME_MAX];
	char path[SUSFS_CREATE_PATH_MAX];
	bool link_old;

	uid_t caller_uid;
	bool caller_is_app;
	bool caller_is_su;
	bool hide_from_apps;
	bool control_visible;

	bool rule_matched;
	bool ordinary_rule;
	bool self_protect;
	bool pending;
	bool is_resolver;
};

struct susfs_create_decision {
	enum susfs_create_action action;
	bool need_parent_permission;
};

static inline struct susfs_create_decision
susfs_create_decide_core(const struct susfs_create_view *v)
{
	struct susfs_create_decision d = {
		.action = SUSFS_CREATE_ERROR,
		.need_parent_permission = false,
	};

	if (!v)
		return d;

	switch (v->state) {
	case SUSFS_CREATE_ST_ABSENT:
		/* A pending path rule blocks the create.  There is no inode owner to
		 * exempt, so the ordinary gate is app-only (or everyone with
		 * hide_from_apps=0); self-protect entries follow control_visible. */
		d.action = SUSFS_CREATE_PASS;
		if (v->rule_matched && v->pending) {
			bool hidden = false;

			if (v->self_protect)
				hidden = !v->control_visible;
			else if (v->ordinary_rule)
				hidden = !v->hide_from_apps || v->caller_is_app;
			if (hidden)
				d.action = SUSFS_CREATE_HIDE;
		}
		return d;
	case SUSFS_CREATE_ST_NOT_NAME:
		d.action = SUSFS_CREATE_NOT_NAME;
		return d;
	case SUSFS_CREATE_ST_ERROR:
		d.action = SUSFS_CREATE_ERROR;
		return d;
	case SUSFS_CREATE_ST_EXISTS:
		break;
	default:
		return d;
	}

	if (!v->rule_matched) {
		d.action = SUSFS_CREATE_PASS;
		return d;
	}

	{
		bool hidden = false;

		if (v->ordinary_rule) {
			/* hide_from_apps=0 applies the set to every process; otherwise the
			 * upstream gate is app-only and never hides the owner's own object. */
			if (!v->hide_from_apps ||
			    (v->caller_is_app && v->caller_uid != v->i_uid))
				hidden = true;
		}
		/* Control nodes are invisible to every non-root caller, not only apps. */
		if (v->self_protect && !v->control_visible)
			hidden = true;
		if (!hidden) {
			d.action = SUSFS_CREATE_PASS;
			return d;
		}
	}

	if (v->kind == SUSFS_CREATE_VIEW_LINKAT) {
		d.action = v->link_old ? SUSFS_CREATE_HIDE_OLD
				       : SUSFS_CREATE_HIDE_NEW;
	} else {
		d.action = SUSFS_CREATE_HIDE;
	}
	d.need_parent_permission = true;
	return d;
}

#endif /* __SUSFS_CREATE_VIEW_H */
