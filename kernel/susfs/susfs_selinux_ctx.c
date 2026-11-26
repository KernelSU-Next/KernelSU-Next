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

#include "btf_layout.h"
#include "symbol_resolver.h"
#include "susfs_selinux_ctx.h"
#include "susfs_log.h"

/* struct context { u32 user, role, type, len; struct mls_range range; char *str; } */
static size_t off_ctx_len, off_ctx_str, ctx_size;
static size_t off_state_policy, off_policy_sidtab, off_policy_policydb;
static unsigned long selinux_state_addr;
static void *pfn_string_to_context_struct;
static void *pfn_sidtab_context_to_sid;

static bool selinux_ctx_test;
module_param_named(selinux_ctx_test, selinux_ctx_test, bool, 0644);

static int susfs_selinux_ctx_resolve(void)
{
	if (!selinux_state_addr)
		selinux_state_addr = find_kernel_symbol_exact("selinux_state");
	if (!pfn_string_to_context_struct)
		pfn_string_to_context_struct = (void *)find_kernel_symbol_exact(
			"string_to_context_struct");
	if (!pfn_sidtab_context_to_sid)
		pfn_sidtab_context_to_sid = (void *)find_kernel_symbol_exact(
			"sidtab_context_to_sid");
	if (!selinux_state_addr || !pfn_string_to_context_struct ||
	    !pfn_sidtab_context_to_sid)
		return -ENOENT;

	if (susfs_btf_layout_init())
		return -ENOENT;
	if (susfs_btf_member_offset("selinux_state", "policy",
				    "selinux_policy *", &off_state_policy) ||
	    susfs_btf_member_offset("selinux_policy", "sidtab", "sidtab *",
				    &off_policy_sidtab) ||
	    susfs_btf_member_offset("selinux_policy", "policydb", "policydb",
				    &off_policy_policydb) ||
	    susfs_btf_member_offset("context", "len", "unsigned int",
				    &off_ctx_len) ||
	    susfs_btf_member_offset("context", "str", "char *",
				    &off_ctx_str) ||
	    susfs_btf_struct_size("context", &ctx_size))
		return -EINVAL;
	return 0;
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
