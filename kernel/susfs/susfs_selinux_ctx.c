// SPDX-License-Identifier: GPL-2.0-only
/*
 * susfs_selinux_ctx.c - force-parse a SELinux context into a sid, from a module.
 *
 * 2.4.0's built-in selinux_hide copies security_context_to_sid_core() and adds a
 * "force" branch: when string_to_context_struct() rejects an undefined context
 * (u:r:su:s0 and friends), the string is stored as an uninterpreted context and
 * sidtab_context_to_sid() still returns a sid.
 *
 * A module cannot include security/selinux/ss, so the three internal symbols are
 * resolved through kallsyms and the struct offsets through vmlinux BTF:
 *   selinux_state.policy -> selinux_policy.sidtab / .policydb
 *   struct context.len / .str
 *
 * Prototype: only runs when selinux_ctx_test=1, logs the result and does not
 * touch any hook.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/cred.h>
#include <linux/dcache.h>
#include <linux/fs.h>

#include "btf_layout.h"
#include "hk.h"		/* hk_resolve: the HooKern symbol lookup */
#include "susfs_selinux_ctx.h"
#include "susfs_log.h"

/* struct context { u32 user, role, type, len; struct mls_range range; char *str; } */
static size_t off_ctx_len, off_ctx_str, ctx_size;
static size_t off_state_policy, off_policy_sidtab, off_policy_policydb;
/* The sid-bearing fields the entry layer updates, all from BTF as well. */
static size_t off_isec_sid;		/* inode_security_struct.sid */
static size_t off_isec_base;		/* SELinux slot inside inode->i_security (per-LSM blob) */
static size_t off_cred_security;	/* cred.security (the SELinux blob) */
static size_t off_tsec_sid;		/* task_security_struct.sid */
static unsigned long selinux_state_addr;
static void *pfn_string_to_context_struct;
static void *pfn_sidtab_context_to_sid;

static bool selinux_ctx_test;
module_param_named(selinux_ctx_test, selinux_ctx_test, bool, 0644);

static int susfs_selinux_ctx_resolve(void)
{
	int rc = 0;

	if (!selinux_state_addr)
		selinux_state_addr = hk_resolve("selinux_state");
	if (!pfn_string_to_context_struct)
		pfn_string_to_context_struct = (void *)hk_resolve(
			"string_to_context_struct");
	if (!pfn_sidtab_context_to_sid)
		pfn_sidtab_context_to_sid = (void *)hk_resolve(
			"sidtab_context_to_sid");
	if (!selinux_state_addr || !pfn_string_to_context_struct ||
	    !pfn_sidtab_context_to_sid) {
		SUSFS_LOGI("selinux_ctx: symbols state=%px s2c=%px s2sid=%px\n",
			   (void *)selinux_state_addr, pfn_string_to_context_struct,
			   pfn_sidtab_context_to_sid);
		return -ENOENT;
	}

	if (susfs_btf_layout_init()) {
		SUSFS_LOGI("selinux_ctx: BTF unavailable\n");
		return -ENOENT;
	}
	if (susfs_btf_member_offset_any("selinux_state", "policy",
					&off_state_policy)) {
		SUSFS_LOGI("selinux_ctx: no selinux_state.policy offset\n");
		rc = -EINVAL;
	}
	if (!rc && susfs_btf_member_offset_any("selinux_policy", "sidtab",
						   &off_policy_sidtab)) {
		SUSFS_LOGI("selinux_ctx: no selinux_policy.sidtab offset\n");
		rc = -EINVAL;
	}
	if (!rc && susfs_btf_member_offset_any("selinux_policy", "policydb",
						   &off_policy_policydb)) {
		SUSFS_LOGI("selinux_ctx: no selinux_policy.policydb offset\n");
		rc = -EINVAL;
	}
	if (!rc && susfs_btf_member_offset_any("context", "len",
						   &off_ctx_len)) {
		SUSFS_LOGI("selinux_ctx: no context.len offset\n");
		rc = -EINVAL;
	}
	if (!rc && susfs_btf_member_offset_any("context", "str",
						   &off_ctx_str)) {
		SUSFS_LOGI("selinux_ctx: no context.str offset\n");
		rc = -EINVAL;
	}
	if (!rc && susfs_btf_struct_size("context", &ctx_size)) {
		SUSFS_LOGI("selinux_ctx: no context size\n");
		rc = -EINVAL;
	}
	if (!rc && susfs_btf_member_offset("inode_security_struct", "sid",
					   "uint", &off_isec_sid)) {
		SUSFS_LOGI("selinux_ctx: no inode_security_struct.sid offset\n");
		rc = -EINVAL;
	}
	if (!rc) {
		/* Android 5.15 allocates inode->i_security as a per-LSM blob; the
		 * SELinux slot starts at selinux_blob_sizes.lbs_inode.  A kernel
		 * without that symbol keeps base 0 (the older single-struct shape).
		 * Not doing this wrote the sid over list.next and panicked. */
		unsigned long blob_sizes = hk_resolve("selinux_blob_sizes");

		off_isec_base = 0;
		if (blob_sizes) {
			size_t lbs_inode_off;

			if (susfs_btf_member_offset("lsm_blob_sizes", "lbs_inode",
						    "int", &lbs_inode_off)) {
				SUSFS_LOGI("selinux_ctx: no lsm_blob_sizes.lbs_inode offset\n");
				rc = -EINVAL;
			} else {
				int val = READ_ONCE(*(int *)((char *)blob_sizes +
							     lbs_inode_off));

				if (val < 0 || val > 4096) {
					SUSFS_LOGI("selinux_ctx: bad lbs_inode %d\n", val);
					rc = -EINVAL;
				} else {
					off_isec_base = (size_t)val;
				}
			}
		}
	}
	if (!rc && susfs_btf_member_offset_any("cred", "security",
					       &off_cred_security)) {
		SUSFS_LOGI("selinux_ctx: no cred.security offset\n");
		rc = -EINVAL;
	}
	if (!rc && susfs_btf_member_offset("task_security_struct", "sid",
					   "uint", &off_tsec_sid)) {
		SUSFS_LOGI("selinux_ctx: no task_security_struct.sid offset\n");
		rc = -EINVAL;
	}
	if (!rc)
		SUSFS_LOGI("selinux_ctx: offsets state.policy=%zu po.sidtab=%zu "
			   "po.policydb=%zu ctx.len=%zu ctx.str=%zu size=%zu "
			   "isec.base=%zu isec.sid=%zu cred.security=%zu tsec.sid=%zu\n",
			   off_state_policy, off_policy_sidtab, off_policy_policydb,
			   off_ctx_len, off_ctx_str, ctx_size, off_isec_base,
			   off_isec_sid, off_cred_security, off_tsec_sid);
	return rc;
}

typedef int (*susfs_string_to_context_fn)(void *policydb, void *sidtab,
					  char *scontext, void *ctx,
					  u32 def_sid);
typedef int (*susfs_sidtab_to_sid_fn)(void *sidtab, void *ctx, u32 *sid);

int __nocfi susfs_security_context_to_sid_force(const char *scontext, u32 len,
						u32 def_sid, u32 *sid)
{
	susfs_string_to_context_fn string_to_context;
	susfs_sidtab_to_sid_fn sidtab_to_sid;
	struct selinux_policy {
		void *sidtab;
	} *policy;
	void *sidtab, *policydb;
	char *buf, *str = NULL;
	void *ctx;
	int rc;

	if (!scontext || !len || !sid)
		return -EINVAL;
	if (susfs_selinux_ctx_resolve())
		return -ENOENT;

	string_to_context = (susfs_string_to_context_fn)pfn_string_to_context_struct;
	sidtab_to_sid = (susfs_sidtab_to_sid_fn)pfn_sidtab_context_to_sid;

	rcu_read_lock();
	policy = READ_ONCE(*(void **)(selinux_state_addr + off_state_policy));
	if (!policy) {
		rcu_read_unlock();
		return -ENOENT;
	}
	sidtab = READ_ONCE(*(void **)((char *)policy + off_policy_sidtab));
	policydb = (char *)policy + off_policy_policydb;

	buf = kmemdup_nul(scontext, len, GFP_ATOMIC);
	if (!buf) {
		rcu_read_unlock();
		return -ENOMEM;
	}

	ctx = kzalloc(ctx_size, GFP_ATOMIC);
	if (!ctx) {
		kfree(buf);
		rcu_read_unlock();
		return -ENOMEM;
	}

	rc = string_to_context(policydb, sidtab, buf, ctx, def_sid);
	if (rc == -EINVAL) {
		/* undefined context: store it as uninterpreted, exactly the
		 * force branch of the built-in helper. */
		str = kstrdup(buf, GFP_ATOMIC);
		if (!str) {
			rc = -ENOMEM;
			goto out;
		}
		*(u32 *)((char *)ctx + off_ctx_len) = strlen(buf) + 1;
		*(char **)((char *)ctx + off_ctx_str) = str;
		rc = 0;
	}
	if (rc)
		goto out;

	rc = sidtab_to_sid(sidtab, ctx, sid);
out:
	kfree(str);
	kfree(ctx);
	kfree(buf);
	rcu_read_unlock();
	return rc;
}

static int selinux_ctx_probe_done;

void susfs_selinux_ctx_probe(void)
{
	static const char probe_ctx[] = "u:r:su:s0";
	u32 sid = 0;
	int rc;

	if (!selinux_ctx_test || selinux_ctx_probe_done)
		return;
	selinux_ctx_probe_done = 1;
	rc = susfs_security_context_to_sid_force(probe_ctx,
						 sizeof(probe_ctx) - 1, 0, &sid);
	SUSFS_LOGI("selinux_ctx: force parse '%s' rc=%d sid=%u\n",
		   probe_ctx, rc, sid);
}

/* setxattr entry: the force sid replaces the inode's SELinux sid, so the
 * relabel the built-in helper writes (isec->sid = newsid) happens here too.
 * 5.15 single-LSM blob: inode->i_security is the inode_security_struct. */
int __nocfi susfs_selinux_ctx_set_inode_sid(struct dentry *dentry,
					    const void *value, size_t size)
{
	struct inode *inode;
	void *isec;
	u32 sid = 0;
	u32 old_sid = 0;
	int rc;

	if (!dentry || !value || !size)
		return -EINVAL;
	rc = susfs_security_context_to_sid_force(value, size, 0, &sid);
	if (rc)
		return rc;
	inode = d_backing_inode(dentry);
	if (!inode || !inode->i_security)
		return -ENOENT;
	isec = (char *)inode->i_security + off_isec_base;
	old_sid = *(u32 *)((char *)isec + off_isec_sid);
	*(u32 *)((char *)isec + off_isec_sid) = sid;
	SUSFS_LOGI("selinux_ctx: set_inode_sid ino=%lu old=%u new=%u base=%zu off=%zu\n",
		   inode->i_ino, old_sid, sid, off_isec_base, off_isec_sid);
	return 0;
}

/* setprocattr entry: the force sid replaces the current task's SELinux sid in
 * a private cred copy, then commit_creds publishes it (the built-in helper's
 * tsec->sid = sid; commit_creds(new)). */
int __nocfi susfs_selinux_ctx_set_current_sid(const void *value, size_t size)
{
	struct cred *cred;
	void *tsec;
	u32 sid = 0;
	u32 old_sid = 0;
	int rc;

	if (!value || !size)
		return -EINVAL;
	rc = susfs_security_context_to_sid_force(value, size, 0, &sid);
	if (rc)
		return rc;
	cred = prepare_creds();
	if (!cred)
		return -ENOMEM;
	tsec = READ_ONCE(*(void **)((char *)cred + off_cred_security));
	if (!tsec) {
		put_cred(cred);
		return -ENOENT;
	}
	old_sid = READ_ONCE(*(u32 *)((char *)tsec + off_tsec_sid));
	*(u32 *)((char *)tsec + off_tsec_sid) = sid;
	commit_creds(cred);
	SUSFS_LOGI("selinux_ctx: set_current_sid old=%u new=%u sec_off=%zu sid_off=%zu\n",
		   old_sid, sid, off_cred_security, off_tsec_sid);
	return 0;
}
