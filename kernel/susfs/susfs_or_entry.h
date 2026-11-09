/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SUSFS_OR_ENTRY_H
#define __SUSFS_OR_ENTRY_H

#include <linux/limits.h>
#include <linux/ptrace.h>

/* The syscall entries susfs_opath.c replaces.  open_redirect's name-layer redirection is asked
 * inside that wrapper rather than installing a wrapper of its own: one syscall entry can have
 * exactly one, and those five belong to the O_PATH layer (which already needs every argument the
 * name layer needs, so nothing is duplicated by sharing).
 *
 * The layer is optional at run time: susfs_or_entry_hook stays NULL until the open_redirect layer
 * found every symbol it needs, and the wrapper tests it before each call, so a missing symbol
 * costs nothing but the feature. */
enum susfs_or_entry_kind {
	SUSFS_OR_ENTRY_OPENAT = 0,
	SUSFS_OR_ENTRY_OPENAT2,
	SUSFS_OR_ENTRY_COMPAT_OPEN,
	SUSFS_OR_ENTRY_COMPAT_OPENAT,
	SUSFS_OR_ENTRY_COMPAT_OPENAT2,
	SUSFS_OR_ENTRY_KIND_COUNT,
};

/* Returned when the hook did not redirect: the caller then runs the original syscall.  LONG_MIN
 * cannot be a real answer here (a fd or -errno), so a mistake cannot be mistaken for one. */
#define SUSFS_OR_ENTRY_PASS	LONG_MIN

typedef long (*susfs_or_entry_fn_t)(const struct pt_regs *regs, int kind);

extern susfs_or_entry_fn_t susfs_or_entry_hook;

/* True once redirection can actually happen.  The open_redirect layer checks this when a rule is added:
 * a rule that cannot fire is worse than no rule, so it refuses rather than register one. */
static inline bool susfs_or_entry_armed(void)
{
	return READ_ONCE(susfs_or_entry_hook) != NULL;
}

/* Both are called from the open_redirect layer's init/exit.  init never fails: the layer is
 * optional, and refusing to arm is reported and survived. */
int susfs_or_entry_init(void);
void susfs_or_entry_exit(void);

#endif /* __SUSFS_OR_ENTRY_H */
