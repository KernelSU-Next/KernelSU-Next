/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_selinux_ctx.h - SELinux context -> sid force parse (prototype)
 */

#ifndef __SUSFS_SELINUX_CTX_H
#define __SUSFS_SELINUX_CTX_H

#include <linux/types.h>

struct dentry;

/* 0 on success, -errno when the policy/struct layout is unavailable.  On
 * success *sid holds a sid the sidtab accepted for the context (an
 * uninterpreted one when the policy does not define it). */
int susfs_security_context_to_sid_force(const char *scontext, u32 len,
					u32 def_sid, u32 *sid);

/* selinux_ctx_test module parameter: run one probe at load and log the result. */
void susfs_selinux_ctx_probe(void);

/* Entry helpers: force-parse the context and write the resulting sid into the
 * inode's SELinux blob (setxattr) or the current task's cred blob
 * (setprocattr).  0 on success, -errno otherwise. */
int susfs_selinux_ctx_set_inode_sid(struct dentry *dentry, const void *value,
				    size_t size);
int susfs_selinux_ctx_set_current_sid(const void *value, size_t size);

#endif /* __SUSFS_SELINUX_CTX_H */
