/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_create_policy.c - create gate policy
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/cred.h>
#include <linux/string.h>
#include <linux/module.h>

#include "susfs.h"
#include "susfs_create_op.h"
#include "susfs_create_policy.h"
#include "susfs_create_view.h"
#include "sus_path_match.h"
#include "ksu_umount_gate.h"
#include "ksu_allow_gate.h"

/* P2d opt-in: take the absent mkdir through the op commit instead of the
 * kernel entry.  Default off until the hidden-absent rules are defined. */
static int create_commit;
module_param(create_commit, int, 0644);

bool susfs_create_commit_enabled(void)
{
	return READ_ONCE(create_commit);
}

static void create_view_fill_caller(struct susfs_create_view *v)
{
	v->caller_uid = current_uid().val;
	v->caller_is_app = susfs_is_current_proc_umounted_app();
	v->caller_is_su = susfs_control_visible();
	v->control_visible = susfs_control_visible();
	v->hide_from_apps = sus_path_hide_from_apps();
}

static int create_view_decide(struct susfs_create_view *v,
			      struct susfs_create_decision *d)
{
	struct sus_path_match m;
	int rc;

	rc = sus_path_rule_match(v, &m);
	if (rc)
		return rc;
	v->rule_matched = m.rule_matched;
	v->ordinary_rule = m.ordinary_rule;
	v->self_protect = m.self_protect;
	v->pending = m.pending;
	*d = susfs_create_decide_core(v);
	return 0;
}

int susfs_create_mkdirat_commit(int dfd, const char __user *path, umode_t mode,
				 int *handled)
{
	struct susfs_create_op *op;
	struct susfs_create_view v;
	int rc;

	*handled = 0;
	op = susfs_create_vfs_begin(dfd, path, &rc);
	if (!op)
		return 0;
	if (!susfs_create_vfs_is_name(op)) {
		susfs_create_vfs_end(op);
		return 0;
	}
	rc = susfs_create_vfs_lock(op);
	if (rc) {
		susfs_create_vfs_end(op);
		return 0;
	}
	rc = susfs_create_vfs_status(op, &v);
	if (rc != 0) {
		/* Positive dentry: the caller's existing-name path handles it. */
		susfs_create_vfs_end(op);
		return 0;
	}
	rc = susfs_create_vfs_commit(op, mode);
	if (rc == -EOPNOTSUPP) {
		susfs_create_vfs_end(op);
		return 0;
	}
	*handled = 1;
	susfs_create_vfs_end(op);
	return rc;
}

bool susfs_create_name_hidden(int dfd, const char __user *path,
			      enum susfs_create_kind kind, bool link_old)
{
	struct susfs_create_view v;
	struct susfs_create_decision d;
	struct susfs_create_op *op;
	bool hidden = false;
	int rc;

	if (!sus_path_user_rules_present())
		return false;

	op = susfs_create_vfs_begin(dfd, path, &rc);
	if (!op)
		return false;

	memset(&v, 0, sizeof(v));
	if (!susfs_create_vfs_is_name(op)) {
		rc = susfs_create_vfs_status(op, &v);	/* NOT_NAME */
		goto out;
	}
	rc = susfs_create_vfs_lock(op);
	if (rc)
		goto out;
	rc = susfs_create_vfs_status(op, &v);
	if (rc < 0)
		goto out;

	v.kind = kind;
	v.link_old = link_old;
	create_view_fill_caller(&v);
	susfs_create_vfs_path(op, v.path, sizeof(v.path));

	rc = create_view_decide(&v, &d);
	if (rc)
		goto out;
	if (d.need_parent_permission)
		susfs_create_vfs_parent_permission_op(op);

	hidden = d.action == SUSFS_CREATE_HIDE ||
		 d.action == SUSFS_CREATE_HIDE_OLD ||
		 d.action == SUSFS_CREATE_HIDE_NEW;
out:
	susfs_create_vfs_end(op);
	return hidden;
}
