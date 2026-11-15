// SPDX-License-Identifier: GPL-2.0
/*
 * susfs_avc_spoof.c - hide the KernelSU su domain from SELinux AVC audit logs
 * (SUSFS AVC_LOG_SPOOFING), LKM port.  This file is the F layer: it owns the
 * hook object, the /proc interface and the supercall, and drives the policy
 * layer for arm/disarm.  The decision itself lives in susfs_avc_policy.c, the
 * state in susfs_avc_data.c.
 */
#include <linux/module.h>
#include <linux/security.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/cred.h>	/* current_uid(), control-node gate */
#include <linux/version.h>	/* LINUX_VERSION_CODE: slow_avc_audit() lost its leading `state` in 6.4 */
#include "susfs_abi.h"
#include "susfs_log.h"
#include "sus_path.h"
#include "susfs_avc_spoof.h"
#include "susfs_avc_data.h"
#include "susfs_avc_policy.h"
#include "susfs_control.h"
#include "susfs_hooks.h"
#include "ksu_allow_gate.h"	/* the control-node gate is su's own */

module_param_string(avc_su_ctx, susfs_avc_su_ctx, sizeof(susfs_avc_su_ctx), 0644);
module_param_string(avc_priv_app_ctx, susfs_avc_priv_app_ctx,
		    sizeof(susfs_avc_priv_app_ctx), 0644);

struct susfs_hook susfs_avc_hook = {
	.owner = "avc_spoof",
	.name = "slow_avc_audit",
	.target = SUSFS_HOOK_TARGET_AVC_AUDIT,
	.pre = susfs_avc_pre_consumer,
};

static int avc_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d (su_sid=%u priv_app_sid=%u enter=%d hits=%d)\n",
		   susfs_avc_spoof_enabled ? 1 : 0, susfs_avc_su_sid,
		   susfs_avc_priv_app_sid,
		   atomic_read(&susfs_avc_enter_count),
		   atomic_read(&susfs_avc_hit_count));
	return 0;
}

static int avc_proc_open(struct inode *inode, struct file *file)
{
	/* 0777 is deliberate (the ENOENT contract comes from sus_path's hidden set, not from the mode), so
	 * refuse non-root callers here too - see the note in susfs_enable_log.c's log_proc_open(). */
	if (!susfs_control_visible())
		return -ENOENT;
	return single_open(file, avc_proc_show, NULL);
}

static ssize_t avc_proc_write(struct file *file, const char __user *buf,
			      size_t len, loff_t *off)
{
	char c;
	int rc = 0;

	if (!susfs_control_visible())
		return -ENOENT;

	if (copy_from_user(&c, buf, 1))
		return -EFAULT;

	if (c == '1') {
		if (!susfs_avc_spoof_enabled) {
			rc = susfs_avc_register(&susfs_avc_hook);
			if (!rc)
				susfs_avc_spoof_enabled = true;
		}
	} else if (c == '0') {
		susfs_avc_unregister(&susfs_avc_hook);
		susfs_avc_spoof_enabled = false;
	} else {
		/* Only '0' and '1' are the protocol. */
		return -EINVAL;
	}

	if (rc) {
		pr_warn("avc_spoof: enable failed %d\n", rc);
		/* Reported to the writer: a switch that did not take effect must not come back as a
		 * complete write.  Success still returns len, the same contract kstat_proc_write()
		 * states for its own commands. */
		return rc;
	}
	return len;
}

static const struct proc_ops avc_proc_ops = {
	.proc_open = avc_proc_open,
	.proc_read = seq_read,
	.proc_write = avc_proc_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static struct proc_dir_entry *avc_proc_entry;

int susfs_avc_spoof_init(void)
{
	int err;

	susfs_avc_data_init();

	err = security_secctx_to_secid(susfs_avc_su_ctx, strlen(susfs_avc_su_ctx),
				       &susfs_avc_su_sid);
	if (err) {
		pr_warn("avc_spoof: secctx_to_secid(%s) failed %d\n",
			susfs_avc_su_ctx, err);
		susfs_avc_su_sid = 0;
	}
	err = security_secctx_to_secid(susfs_avc_priv_app_ctx,
				       strlen(susfs_avc_priv_app_ctx),
				       &susfs_avc_priv_app_sid);
	if (err) {
		pr_warn("avc_spoof: secctx_to_secid(%s) failed %d\n",
			susfs_avc_priv_app_ctx, err);
		susfs_avc_priv_app_sid = 0;
	}
	SUSFS_LOGI("avc_spoof: su_sid=%u (%s), priv_app_sid=%u (%s)\n",
		susfs_avc_su_sid, susfs_avc_su_ctx,
		susfs_avc_priv_app_sid, susfs_avc_priv_app_ctx);

	/* Only the /proc node is optional: susfs_avc_register() installs the hook whenever the feature is
	 * switched on, supercall included.  0777 so DAC passes and sus_path's LSM layer gets to answer ENOENT. */
	if (susfs_control_node_allowed()) {
		avc_proc_entry = proc_create("susfs_avc_spoof", 0777, NULL,
					     &avc_proc_ops);
		if (!avc_proc_entry)
			pr_warn("proc_create(susfs_avc_spoof) failed\n");
		else
			SUSFS_LOGI("susfs_avc_spoof: proc ready (/proc/susfs_avc_spoof)\n");
	} else {
		SUSFS_LOGI("susfs_avc_spoof: /proc node not created (expose_proc=%d lsm=%d)\n",
			(int)susfs_expose_proc, (int)sus_path_lsm_active());
	}
	return 0;
}

void susfs_avc_spoof_exit(void)
{
	/* One critical section for "no more registrations" and the disarm: a /proc write holds no
	 * module reference and can be inside avc_proc_write() right now. */
	susfs_avc_unregister_for_exit(&susfs_avc_hook);

	susfs_avc_spoof_enabled = false;
	if (avc_proc_entry) {
		proc_remove(avc_proc_entry);
		avc_proc_entry = NULL;
	}
}

/* supercall: CMD_SUSFS_ENABLE_AVC_LOG_SPOOFING */
void susfs_avc_spoof_supercall(void __user **arg)
{
	struct st_susfs_avc_log_spoofing info = {0};

	if (copy_from_user(&info, (void __user *)*arg, sizeof(info))) {
		info.err = -EFAULT;
		goto out;
	}

	if (info.enabled) {
		if (!susfs_avc_spoof_enabled) {
			int rc = susfs_avc_register(&susfs_avc_hook);

			if (rc) {
				info.err = rc;
				goto out;
			}
			susfs_avc_spoof_enabled = true;
		}
	} else {
		susfs_avc_unregister(&susfs_avc_hook);
		susfs_avc_spoof_enabled = false;
	}
	info.err = 0;
	SUSFS_LOGI("avc_spoof: %s (supercall)\n", info.enabled ? "enabled" : "disabled");
out:
	/* upstream writes back only ->err for input-type commands */
	if (copy_to_user(&((struct st_susfs_avc_log_spoofing __user *)*arg)->err,
			 &info.err, sizeof(info.err)))
		pr_warn("avc supercall copy_to_user failed\n");
}
