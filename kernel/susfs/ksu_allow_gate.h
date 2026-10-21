/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SUSFS_KSU_ALLOW_GATE_H
#define __SUSFS_KSU_ALLOW_GATE_H

#include <linux/cred.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/version.h>

/* Whether the KernelSU set can ever be let through susfs_control_visible(): only where
 * susfs_relabel_control_nodes() can put the ksu_file label on the control nodes, which is below
 * 6.4 (susfs_main.c returns early above it).  Used for what the module says at load, not for the
 * gate itself - susfs_control_relabelled is still what the gate reads. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
#define SUSFS_CONTROL_GATE_CAN_WIDEN 0
#else
#define SUSFS_CONTROL_GATE_CAN_WIDEN 1
#endif

/* "Is this process one KernelSU would allow?" - asked OF KernelSU, not re-derived here.
 *
 * The question this answers is not "is it root".  KernelSU's own predicate is
 * kernel/policy/allowlist.c:
 *     bool __ksu_is_allow_uid_for_current(uid_t uid)
 *     {
 *         if (unlikely(uid == 0))
 *                 return is_ksu_domain();     // root only counts in the ksu domain
 *         return __ksu_is_allow_uid(uid);     // the allowlist, plus the manager app
 *     }
 * so one call covers the ksu-domain root, the manager and every su-granted uid - and it is the same
 * answer KernelSU itself acts on, instead of a re-implementation that can drift from it.
 *
 * This is deliberately NOT TIF_SYSCALL_TRACEPOINT / syscall_work: KernelSU sets that bit for every
 * process whose syscalls it wants to see (its own tp_marker.c marks zygote, the adb shell and - when
 * another tracer is present - every process), and the bit is inherited across fork with nothing
 * clearing it.  It answers "will KernelSU's hook run for this task", not "does KernelSU trust it". */

/* WHICH allow source is in force.  0 = none found (the strict fallback, uid 0 only),
 * 1 = KernelSU's __ksu_is_allow_uid_for_current.  This is the extension point: a second source -
 * another root solution's predicate, or an extra local rule - becomes case 2 in
 * susfs_ksu_allow_ask() without touching a single caller, because callers only ever see
 * susfs_ksu_allow_uid()/susfs_ksu_allow_current().  Set once by the init, read by everybody. */
extern int susfs_ksu_allow_kind;

/* The one indirect call, through the address resolved at load. */
bool susfs_ksu_allow_ask(uid_t uid);

/* The whole predicate for the current process.  Fail-safe on the STRICT side: without KernelSU's
 * symbol the answer is "real root only", so a missing or renamed KernelSU loses features (the
 * privileged view stops being visible to su) instead of leaking it to apps. */
static inline bool susfs_ksu_allow_uid(uid_t uid)
{
	if (READ_ONCE(susfs_ksu_allow_kind) == 0)
		return uid == 0;	/* nothing resolved: the strict answer, by design */

	return susfs_ksu_allow_ask(uid);
}

static inline bool susfs_ksu_allow_current(void)
{
	return susfs_ksu_allow_uid(current_uid().val);
}

/* Set by susfs_relabel_control_nodes() once every control node it found carries the ksu_file
 * label.  Until then the nodes keep proc's own label, whose policy answers EACCES to the shell
 * and to su-granted apps: letting those callers past this gate would turn the hidden nodes'
 * ENOENT into an EACCES, which proves the nodes exist - the one thing the gate is for.  So the
 * KernelSU set applies only where SELinux will also let it read, root-only otherwise. */
extern bool susfs_control_relabelled;

/* "May this process use the module's control surface?"  The SAME gate KernelSU applies before it
 * would give this uid su: sucompat calls ksu_is_allow_uid_for_current(), which allowlist.h defines
 * as exactly this predicate - so it carries forbid_system_uid, "manager is always allowed", the
 * allowed-shell rule and the allowlist profile's allow_su, and __ksu_is_allow_uid_for_current()
 * adds the shell-only check before asking.  Every control node's open/write handler and sus_path's
 * self-protect rules ask this one question, which is what keeps "the nodes are visible" and "su is
 * available" the same set of processes instead of two policies that can drift apart.
 *
 * That set is only usable where the nodes carry a label those callers may read.  When the relabel
 * did not happen - every >= 6.4 build, a failed or partial relabel, or a node whose inode was
 * evicted and recreated under proc's label since - the gate falls back to root-only, which is what
 * the nodes' own label says anyway.  Widening the gate without the label is what turned the
 * nodes' ENOENT into an EACCES that proved they existed (susfs4ksu-lkm#108). */
static inline bool susfs_control_visible(void)
{
	if (!READ_ONCE(susfs_control_relabelled))
		return current_uid().val == 0;
	return susfs_ksu_allow_current();
}

int susfs_ksu_allow_gate_init(void);

#endif /* __SUSFS_KSU_ALLOW_GATE_H */