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

/* 0 keeps the pre-view decision (P2a rollback), 1 is the P2b default. */
static int create_view = 1;
module_param(create_view, int, 0644);

static void create_view_fill(struct susfs_create_view *v, struct inode *inode,
			     enum susfs_create_kind kind, bool link_old,
			     const char *name)
{
	memset(v, 0, sizeof(*v));
	v->kind = kind;
	v->state = SUSFS_CREATE_ST_EXISTS;
	v->link_old = link_old;
	v->ino = inode->i_ino;
	v->s_dev = inode->i_sb->s_dev;
	v->i_uid = inode->i_uid.val;
	v->sb_magic = inode->i_sb->s_magic;
	v->caller_uid = current_uid().val;
	v->caller_is_app = susfs_is_current_proc_umounted_app();
	v->caller_is_su = susfs_control_visible();
	v->control_visible = susfs_control_visible();
	v->hide_from_apps = sus_path_hide_from_apps();
	if (name)
		strscpy(v->name, name, sizeof(v->name));
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
	if (rc <= 0)
		goto out;

	v.kind = kind;
	v.link_old = link_old;
	create_view_fill_caller(&v);

	if (!READ_ONCE(create_view)) {
		struct inode *inode = susfs_create_vfs_inode(op);

		hidden = inode && sus_path_inode_hidden_public(inode);
		goto out;
	}

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

int susfs_create_existing_answer(struct inode *inode)
{
	struct susfs_create_view v;
	struct susfs_create_decision d;

	if (!inode)
		return -EEXIST;
	if (!READ_ONCE(create_view))
		return sus_path_inode_hidden_public(inode) ? -EACCES : -EEXIST;
	create_view_fill(&v, inode, SUSFS_CREATE_VIEW_MKDIRAT, false, NULL);
	if (create_view_decide(&v, &d))
		return -EEXIST;
	return d.action == SUSFS_CREATE_HIDE ? -EACCES : -EEXIST;
}
