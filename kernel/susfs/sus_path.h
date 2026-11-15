/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SUS_PATH_H
#define __SUS_PATH_H

#include <linux/types.h>
#include <linux/compiler_types.h>

/* Add a path to sus_path's hidden set from kernel code (no supercall needed); returns 0 or negative errno. */
int sus_path_add_hidden(const char *path);

/* Same, for one of this module's own control nodes: flagged so the gate hides it from EVERY non-root caller, not merely
 * from app processes - otherwise a probe running as system (1000) or shell (2000) reads the node name out of /proc. */
int sus_path_add_self_hidden(const char *path);

/* Undo sus_path_add_self_hidden(): drops the rule for @path and restores whatever it changed (the relaxed mode, the inode
 * reference); returns the number of rules removed, so 0 means "was not registered".  Process context only (iput). */
int sus_path_del_path(const char *path);

/* sus_path's own control node: `cat` is the rule listing (the hide_list parameter view), writing takes add/del/clear.  An
 * operator who cannot see the rule table cannot tell "registered" from "hiding"; root-only, everyone else gets ENOENT. */
#define SUSFS_PATH_NODE "/proc/susfs_path"

int sus_path_init(void);
void sus_path_exit(void);

void sus_path_supercall(unsigned int cmd, void __user **arg);

bool sus_path_lsm_active(void);

/* sus_path's hidden decision, for the layers that hold the inode the kernel resolved rather than a path they would have to
 * resolve themselves (susfs_opath.c post-checks the file an open(..., O_PATH) produced).  Same question the LSM hooks ask,
 * same answer: identity first, then (dev, i_ino), never a path string.  Not a new judgement - it is the static
 * sus_path_inode_hidden() with a name other translation units can call, so O_PATH and the ordinary open path cannot drift
 * apart.  NULL inode means "not hidden" (a negative dentry). */
struct inode;
bool sus_path_inode_hidden_public(struct inode *inode);

/* Whether any sus_path rule is live right now: the create-family entries test this before
 * they resolve a path, so an empty table costs one load and nothing else. */
bool sus_path_rules_present(void);

/* Whether any ordinary (operator-supplied) sus_path rule is live.  Self-protect entries for the
 * module's own /proc nodes are deliberately excluded: the create family must stay a fast pass
 * when only those exist. */
bool sus_path_user_rules_present(void);

#endif /* __SUS_PATH_H */
