/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SUSFS_KSU_UMOUNT_GATE_H
#define __SUSFS_KSU_UMOUNT_GATE_H

#include <linux/cred.h>
#include <linux/sched.h>
#include <linux/types.h>

/* Set once, by ksu_umount_gate.c's init, from whether KernelSU's symbol was found: false = the gates
 * ask KernelSU, true = answer the pre-#34 way (uid >= 10000) without calling anything.  Read by the
 * gates, never recomputed, and never a loader parameter: the only thing that matters is one fact
 * about the device, and it is established where the module can still see the whole picture. */
extern bool susfs_ksu_umount_gate_off;

/* Defined in ksu_umount_gate.c: the one indirect call, through the address resolved at load. */
bool susfs_ksu_umount_ask(uid_t uid);

/* Upstream's susfs_is_current_proc_umounted_app(): app uid AND KernelSU would umount for it.  The
 * uid half is upstream's literal 10000; the flag half is the predicate that flag is set from, so a
 * su-granted app, the manager and a profile with "umount modules" off are not hidden from. */
#define SUSFS_PER_USER_RANGE		100000
#define SUSFS_FIRST_ISOLATED_UID	99000
#define SUSFS_LAST_ISOLATED_UID		99999

static inline bool susfs_uid_is_isolated(uid_t uid)
{
	uid_t appid = uid % SUSFS_PER_USER_RANGE;

	return appid >= SUSFS_FIRST_ISOLATED_UID && appid <= SUSFS_LAST_ISOLATED_UID;
}

static inline bool susfs_is_current_proc_umounted_app(void)
{
	uid_t uid = current_uid().val;

	if (uid < 10000)
		return false;
	if (READ_ONCE(susfs_ksu_umount_gate_off))
		return true;	/* nothing resolved to ask: hide from app uids, as this module did before #34 */

	/* KernelSU has no profile for an isolated uid, so asking it answers the global default; both
	 * its kernel_umount.c:97 (`|| is_isolated_process`) and the SUSFS patch force umount for them. */
	if (susfs_uid_is_isolated(uid))
		return true;

	return susfs_ksu_umount_ask(uid);
}

/* The uids KernelSU itself asks the umount question about - the gate at
 * kernel/feature/kernel_umount.c:93 of an unpatched KernelSU
 * (`is_appuid(new_uid) || new_uid == WEBVIEW_ZYGOTE_UID || is_isolated_process(new_uid)`, with
 * is_appuid()/is_isolated_process() and the range constants from kernel/policy/allowlist.h:8-13).
 *
 * Deliberately the set KernelSU ASKS about, and not the set SUSFS's own TIF_PROC_UMOUNTED flag can
 * be set for: that flag is SUSFS's (kernel_patches/include/linux/susfs_def.h:64), and the patch
 * that sets it (kernel_patches/KernelSU/10_enable_susfs_for_ksu.patch, in kernel/hook/setuid_hook.c)
 * does so for `is_isolated_process(ruid) || (is_appuid(ruid) && ksu_uid_should_umount(ruid))` - a
 * narrower set that never includes the webview zygote (both predicates are false for uid 1053) and
 * that additionally requires the caller to be a zygote child.
 *
 * Asking ksu_uid_should_umount() about a uid outside this set does not answer the same question
 * either way: for a uid with no profile it returns default_non_root_profile.umount_modules, and
 * that value is TRUE (kernel/policy/allowlist.c:54-55) - so uid 0, 1000 and 2000 would all be
 * answered "yes" without the boundary below. */
static inline bool susfs_ksu_umount_uid_applies(uid_t uid)
{
	uid_t appid = uid % 100000;			/* PER_USER_RANGE */

	return (appid >= 10000 && appid <= 19999) ||	/* is_appuid() */
	       susfs_uid_is_isolated(uid) ||		/* is_isolated_process() */
	       uid == 1053;				/* WEBVIEW_ZYGOTE_UID */
}

/* Upstream's susfs_is_current_proc_umounted(): the same umount answer WITHOUT the app-uid half,
 * which is what its UID_UMOUNTED_PROC rule scheme asks for (and what makes that scheme cover
 * init-derived processes too).  There is deliberately no uid fallback when KernelSU's symbol did
 * not resolve: there would be nothing to ask, and answering "true" would hide from every process
 * alive - false is what the upstream flag does on a kernel without the SUSFS patch anyway.
 *
 * The boundary is the set KernelSU asks about, and it is deliberately NOT the upstream flag.  The
 * flag is inherited across fork and never cleared - nothing calls
 * susfs_clear_current_proc_umounted() - so upstream answers true for, say, a su that descends from
 * an already-umounted app, and it is also never set for the webview zygote (matching on it was
 * removed when the flag moved to the SUSFS patch).  Here every uid outside the set answers false,
 * su included, and 1053 is inside it.  What this gate mirrors is the question the umount schemes
 * are actually about - "would KernelSU umount for this uid" - and that one has a definite answer
 * for root, init, system_server and the adb shell instead of the "no profile, therefore umount"
 * default. */
static inline bool susfs_is_current_proc_umounted(void)
{
	uid_t uid = current_uid().val;

	if (READ_ONCE(susfs_ksu_umount_gate_off))
		return false;
	/* KernelSU forces isolated uids to umount (kernel_umount.c:97 `|| is_isolated_process`) and
	 * the SUSFS patch sets TIF_PROC_UMOUNTED for them; the no-uid-half scheme must match. */
	if (susfs_uid_is_isolated(uid))
		return true;
	if (!susfs_ksu_umount_uid_applies(uid))
		return false;

	return susfs_ksu_umount_ask(uid);
}

int susfs_ksu_umount_gate_init(void);

#endif /* __SUSFS_KSU_UMOUNT_GATE_H */
