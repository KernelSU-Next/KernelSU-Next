// SPDX-License-Identifier: GPL-2.0
/*
 * sus_dirent.c - answer getdents64 without ever writing a hidden name.
 *
 * F layer: decode the syscall arguments, own the file reference and the
 * init/exit.  The filldir record layout is in susfs_dirent_phys.c, the
 * decision in susfs_dirent_policy.c and the state in susfs_dirent_data.c.
 *
 * sus_path_dirent_filter() runs after the syscall has filled the caller's
 * buffer, and two public detectors read that directly:
 *
 *   - a chunk whose records are all hidden has no honest answer, so the
 *     rewrite leaves one record with d_ino = 0 and an empty name.  A real
 *     filesystem never emits d_ino 0, and a 64-byte buffer puts one record per
 *     chunk, so the placeholder is handed over alone.
 *   - the rewrite is copy_to_user()/clear_user() over memory the caller still
 *     owns.  A second thread flipping it PROT_NONE and back faults the scrub,
 *     and the "filtering would be worse than not filtering" fallback then
 *     returns the chunk UNFILTERED.
 *
 * Both follow from rewriting the caller's buffer, so this layer does not
 * rewrite it: it runs iterate_dir() with its own filldir, which simply does not
 * emit a hidden name.
 */

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/uaccess.h>
#include <asm/ptrace.h>

#include "susfs_dirent.h"
#include "susfs_dirent_data.h"
#include "susfs_dirent_phys.h"
#include "susfs_dirent_policy.h"
#include "susfs_log.h"
#include "symbol_resolver.h"

__nocfi bool susfs_dirent_prefilter(const struct pt_regs *regs, long *out,
				    bool compat)
{
	unsigned int fd = (unsigned int)regs->regs[0];
	void __user *dirp = (void __user *)regs->regs[1];
	unsigned int count = (unsigned int)regs->regs[2];
	struct file *f;
	long error;

	if (!susfs_dirent_prefilter_enabled())
		return false;

	/* The kernel's own bound for a getdents buffer.  Without it a huge
	 * caller-supplied count reaches the filldir room arithmetic as a value
	 * the kernel would never have used. */
	if (count > MAX_RW_COUNT)
		count = MAX_RW_COUNT;

	atomic_inc(&susfs_dirent_calls);

	f = susfs_dirent_fget(fd);
	if (!f) {
		*out = -EBADF;
		return true;
	}

	error = susfs_dirent_run(f, dirp, count, compat);
	fput(f);
	*out = error;
	return true;
}

int susfs_sus_dirent_init(void)
{
	susfs_dirent_iterate_dir = (void *)find_kernel_symbol_exact("iterate_dir");
	susfs_dirent_fget = (void *)find_kernel_symbol_exact("fget");
	if (!susfs_dirent_iterate_dir || !susfs_dirent_fget) {
		pr_warn("sus_dirent: iterate_dir=%px fget=%px - staying off, the post-hoc filter keeps hiding\n",
			(void *)susfs_dirent_iterate_dir, (void *)susfs_dirent_fget);
		return 0;
	}
	WRITE_ONCE(susfs_dirent_armed, true);
	SUSFS_LOGI("sus_dirent: armed, iterate_dir=%px\n",
		   (void *)susfs_dirent_iterate_dir);
	return 0;
}

void susfs_sus_dirent_exit(void)
{
	WRITE_ONCE(susfs_dirent_armed, false);
	SUSFS_LOGI("sus_dirent: calls=%d hidden=%d\n",
		   atomic_read(&susfs_dirent_calls), atomic_read(&susfs_dirent_hidden));
}
