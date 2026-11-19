// SPDX-License-Identifier: GPL-2.0
/*
 * susfs_or_entry.c - open_redirect's forward direction, done at the NAME layer.
 *
 * Why this exists: the vfs_open kprobe has to keep a cached struct path per rule, because by the
 * time vfs_open runs the name is already resolved and nothing can sleep there.  Cached dentry and
 * mount references are what stop a filesystem teardown from finishing while a rule points into it.
 * Upstream has no such problem because it patches do_sys_openat2, where it simply builds a new
 * struct filename from the stored string; those symbols are LTO-inlined here and cannot be probed,
 * but the syscall entry can be replaced - susfs_opath.c already replaces exactly the five entries
 * this needs - so the same thing can be done at the same layer as upstream.
 *
 * What it does per matching open, in process context before the syscall runs:
 *   getname_flags() + filename_lookup()  -> which inode is the caller opening (relative paths too)
 *   susfs_open_redirect_match_path()     -> is that the target of a live rule, and where does it go
 *   filp_open()                          -> open the target ourselves
 *   get_unused_fd_flags() + fd_install() -> hand the caller a fd, exactly like the syscall would
 *   and return the fd instead of running the original syscall.
 *
 * Nothing is cached, so a rule holds no reference of any kind: this is what makes open_redirect
 * stop mattering to umount.  The functions are resolved by name (filp_open, get_unused_fd_flags and
 * fd_install are exported, getname_flags, filename_lookup and putname are not) and the layer stays
 * off if any of them is missing.
 *
 * Skipping the original syscall means whatever do_sys_openat2() would have done around the open has
 * to be done here as well.  Two things are: the arm64 O_LARGEFILE bit the syscall adds on the entries
 * that set it (otherwise every redirected fd differs from every other fd in one bit that
 * fcntl(F_GETFL) and the fdinfo `flags:` line both read), and the IN_OPEN notification where the
 * kernel happens to have an out-of-line fsnotify_open() - it is a static inline on every tree, and
 * the log says so where it is missing.  What is deliberately NOT reproduced, and is therefore handed
 * back to the kernel instead:
 *   - the flags openat2 would refuse (it validates its structure field by field, where the older
 *     entries clean invalid values up and carry on), along with its `resolve`/`usize` (EINVAL/E2BIG,
 *     and RESOLVE_* is applied during the walk) - finishing such an open would answer success where
 *     the kernel answers an error, or answer with weaker semantics;
 *   - O_TMPFILE, which is not a path open in the sense filp_open() models.
 * Not covered at all, by construction: this is a syscall entry, so covers openat/openat2 and the
 * three compat variants.  io_uring's OPENAT/OPENAT2, AArch32 `creat`, open_by_handle_at, execve and
 * in-kernel filp_open() callers never come through here (upstream's do_sys_openat2 patch has the same
 * boundary except for compat `creat`, which upstream also misses).
 *
 * On by default (module parameter or_entry).  It used to be opt-in, back when the vfs_open probe was
 * still the default forward direction; that probe is gone, so this layer IS the forward direction -
 * which is also why or_register() refuses a rule while it is not armed, and why an unarmed layer
 * says so in the log instead of failing silently.
 */
#include <linux/kernel.h>
#include <linux/version.h>	/* LINUX_VERSION_CODE: fsnotify_open() moved into the open path at 6.6 */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include "susfs_opath.h"
#include "susfs_open_redirect.h"
#include "susfs_log.h"
#include "susfs_or_entry.h"
#include "susfs_or_entry_data.h"
#include "susfs_or_entry_phys.h"

/* include/uapi/linux/openat2.h: three u64 fields, identical on both ABIs we build for. */
struct susfs_open_how {
	__u64 flags;
	__u64 mode;
	__u64 resolve;
};

susfs_or_entry_fn_t susfs_or_entry_hook;

extern long susfs_or_entry_redirect(const struct pt_regs *regs, int kind);

int susfs_or_entry_init(void)
{
	/* or_entry=0 means "no redirection at the name layer", and the rule-management path asks this
	 * layer whether it is armed: leaving the hook installed but inert would accept rules that can
	 * never fire, so the parameter is honoured here and the hook simply stays NULL. */
	if (!READ_ONCE(susfs_or_entry_param)) {
		SUSFS_LOGI("or_entry=0: name layer off (open_redirect will refuse rules)\n");
		return 0;
	}

	/* The five open entries this layer needs are the ones susfs_opath.c installs: asking whether the
	 * name layer is armed has to mean "at least one of those entries really carries a wrapper", or a
	 * rule would be accepted from a device where opath_dump=1 / opath_test=N / a failed install left
	 * the entries untouched - a rule that can never fire. */
	if (!susfs_opath_armed()) {
		SUSFS_LOGI("or_entry: susfs_opath installed none of the open entries - the name layer stays off (open_redirect will refuse rules)\n");
		return 0;
	}

	if (susfs_or_entry_symbols_resolve())
		return 0;

	susfs_or_entry_ready = true;
	smp_wmb();
	WRITE_ONCE(susfs_or_entry_hook, susfs_or_entry_redirect);
	SUSFS_LOGI("or_entry: name-layer redirection armed; open_redirect rules hold no inode while it is on\n");
	return 0;
}

void susfs_or_entry_exit(void)
{
	WRITE_ONCE(susfs_or_entry_hook, NULL);
	smp_mb();
	susfs_or_entry_ready = false;
}
