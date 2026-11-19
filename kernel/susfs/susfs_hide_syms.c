/* SPDX-License-Identifier: GPL-2.0-only */
/* Feature entry: proc/parameter decoding and hook registration descriptors. */
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include "susfs_log.h"
#include "sus_path.h"
#include "susfs_control.h"
#include "susfs_hide_syms.h"
#include "susfs_hooks.h"
#include "ksu_allow_gate.h"
#include "susfs_hide_names.h"
#include "susfs_hide_policy.h"
#include "susfs_hide_phys.h"

static int hide_modules_param_set(const char *val, const struct kernel_param *kp)
{
	return susfs_hide_param_set(val);
}

static int hide_modules_param_get(char *buf, const struct kernel_param *kp)
{
	return hide_modules_format(buf, PAGE_SIZE);
}

static const struct kernel_param_ops hide_modules_ops = {
	.get = hide_modules_param_get,
	.set = hide_modules_param_set,
};
/* 0600: root reads and writes it, and the whole directory is inside the one the hide_modules feature hides from everyone. */
module_param_cb(hide_modules, &hide_modules_ops, NULL, 0600);

static int hide_modules_proc_show(struct seq_file *m, void *v)
{
	char buf[512];

	hide_modules_format(buf, sizeof(buf));
	seq_puts(m, buf);
	return 0;
}

static int hide_modules_proc_open(struct inode *inode, struct file *file)
{
	/* 0777 node + this check, exactly like the other control nodes: a restrictive mode would answer EACCES (which
	 * advertises that the node exists) before sus_path could answer ENOENT. */
	if (!susfs_control_visible())
		return -ENOENT;
	return single_open(file, hide_modules_proc_show, NULL);
}

static ssize_t hide_modules_proc_write(struct file *file, const char __user *buf,
				       size_t len, loff_t *off)
{
	char cmd[HIDE_MODULES_CMDLINE];
	int rc;

	/* Same reason as the open check: an fd opened before the process dropped privileges must not become a way in. */
	if (!susfs_control_visible())
		return -ENOENT;
	if (len == 0)
		return 0;
	if (len >= sizeof(cmd))
		return -EINVAL;
	if (copy_from_user(cmd, buf, len))
		return -EFAULT;
	cmd[len] = '\0';

	/* The node takes commands only: a typo must not silently replace the list. */
	rc = hide_modules_command(cmd, false);
	if (rc)
		return rc;
	return len;
}

static const struct proc_ops hide_modules_proc_ops = {
	.proc_open = hide_modules_proc_open,
	.proc_read = seq_read,
	.proc_write = hide_modules_proc_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static struct proc_dir_entry *hide_modules_entry;

static struct susfs_hook hide_s_reg = {
	.owner = "hide_syms",
	.name = "s_show",
	.target = SUSFS_HOOK_TARGET_S_SHOW,
	.stub = susfs_hide_s_stub,
	.tramp = &susfs_hide_s_tramp,
};
static struct susfs_hook hide_m_reg = {
	.owner = "hide_syms",
	.name = "m_show",
	.target = SUSFS_HOOK_TARGET_M_SHOW,
	.stub = susfs_hide_m_stub,
	.tramp = &susfs_hide_m_tramp,
};
int susfs_hide_syms_init(void)
{
	int rc = susfs_hide_config_init();

	if (rc)
		return rc;
	rc = susfs_hide_phys_init(&hide_s_reg, &hide_m_reg);
	if (rc)
		return rc;
	susfs_hide_seed_self();
	/* The runtime control node: created only when the LSM layer that hides it is installed, so an unprotected
	 * world-writable node cannot exist (same shape as the other control nodes - see susfs_control_node_allowed()). */
	if (susfs_control_node_allowed()) {
		hide_modules_entry = proc_create("susfs_hide_modules", 0777, NULL,
						 &hide_modules_proc_ops);
		if (!hide_modules_entry)
			pr_warn("susfs_hide_syms: proc_create(susfs_hide_modules) failed - runtime control unavailable, use the hide_modules parameter\n");
	} else {
		SUSFS_LOGI("susfs_hide_syms: /proc/susfs_hide_modules not created (expose_proc=%d lsm=%d)\n",
			(int)susfs_expose_proc, (int)sus_path_lsm_active());
	}

	return 0;
}

bool susfs_hide_syms_active(void)
{
	return susfs_hide_phys_active();
}

bool susfs_hide_modules_active(void)
{
	return susfs_hide_has_modules();
}

bool susfs_hide_modules_node_ready(void)
{
	return hide_modules_entry != NULL;
}

void susfs_hide_syms_exit(void)
{
	if (hide_modules_entry) {
		proc_remove(hide_modules_entry);
		hide_modules_entry = NULL;
	}
	susfs_hide_phys_exit(&hide_s_reg, &hide_m_reg);
	susfs_hide_policy_exit();
}
