/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_selinux_ctx.h - SELinux context -> sid force parse (prototype)
 */

#ifndef __SUSFS_SELINUX_CTX_H
#define __SUSFS_SELINUX_CTX_H

#include <linux/types.h>

/* 0 on success, -errno when the policy/struct layout is unavailable.  On
 * success *sid holds a sid the sidtab accepted for the context (an
 * uninterpreted one when the policy does not define it). */
int susfs_security_context_to_sid_force(const char *scontext, u32 len,
					u32 def_sid, u32 *sid);

/* selinux_ctx_test module parameter: run one probe at load and log the result. */
void susfs_selinux_ctx_probe(void);

#endif /* __SUSFS_SELINUX_CTX_H */
