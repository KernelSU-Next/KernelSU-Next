/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_enable_log_phys.c - /proc/susfs_enable_log node (P layer)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#include "ksu_allow_gate.h"	/* susfs_control_visible() */
#include "sus_path.h"
#include "susfs_control.h"
#include "susfs_enable_log_data.h"
#include "susfs_enable_log_phys.h"
#include "susfs_enable_log_policy.h"
#include "susfs_log.h"
#include "susfs_main_data.h"

static int log_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", susfs_log_enabled() ? 1 : 0);
	return 0;
}

static int log_proc_open(struct inode *inode, struct file *file)
{
	/* 0777 on purpose: the ENOENT contract for non-root callers comes from
	 * sus_path's hidden set, and a restrictive mode would answer EACCES -
	 * which leaks that the node exists.  That makes sus_path's hook the only
	 * thing between an app and this interface, so the handler refuses
	 * non-root callers itself as well. */
	if (!susfs_control_visible())
		return -ENOENT;
	return single_open(file, log_proc_show, NULL);
}

static ssize_t log_proc_write(struct file *file, const char __user *buf,
			      size_t len, loff_t *off)
{
	char c;

	/* Same reason as the open check: an fd opened before the process dropped
	 * privileges must not become a way in. */
	if (!susfs_control_visible())
		return -ENOENT;

	if (copy_from_user(&c, buf, 1))
		return -EFAULT;

	/* Only '0' and '1' are the protocol.  The old code accepted every other
	 * byte, still reported len, and toggled nothing - a typo was
	 * indistinguishable from success. */
	if (c != '0' && c != '1')
		return -EINVAL;

	susfs_enable_log_set(c == '1', NULL);
	return len;
}

static const struct proc_ops log_proc_ops = {
	.proc_open = log_proc_open,
	.proc_read = seq_read,
	.proc_write = log_proc_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static struct proc_dir_entry *log_proc_entry;

int susfs_enable_log_proc_init(void)
{
	/* Nothing to register here (logging is toggled over the supercall or by
	 * this node), so the node is the only thing to gate.  See
	 * susfs_control_node_allowed(): 0777 so DAC passes, LSM answers ENOENT. */
	if (susfs_control_node_allowed()) {
		log_proc_entry = proc_create("susfs_enable_log", 0777, NULL,
					     &log_proc_ops);
		if (!log_proc_entry)
			pr_warn("proc_create(susfs_enable_log) failed\n");
		else
			SUSFS_LOGI("susfs_enable_log: armed (proc: /proc/susfs_enable_log)\n");
	} else {
		SUSFS_LOGI("susfs_enable_log: /proc node not created (expose_proc=%d lsm=%d)\n",
			(int)susfs_expose_proc, (int)sus_path_lsm_active());
	}
	return 0;
}

void susfs_enable_log_proc_exit(void)
{
	if (log_proc_entry) {
		proc_remove(log_proc_entry);
		log_proc_entry = NULL;
	}
}
