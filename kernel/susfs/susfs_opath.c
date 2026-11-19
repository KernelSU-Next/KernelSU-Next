// SPDX-License-Identifier: GPL-2.0
/*
 * susfs_opath.c - hide sus_path's files from open(..., O_PATH).
 *
 * Why the LSM layer cannot see an O_PATH open
 * -------------------------------------------
 * do_dentry_open() answers O_PATH before anything that could refuse: it sets
 * f_mode = FMODE_PATH | FMODE_OPENED and f_op = &empty_fops and returns, and that return is
 * BEFORE security_file_open() (fs/open.c:801-805 against :832 in the 5.15 tree this was
 * written against).  Neither that early return nor the final-component walk of an O_PATH
 * lookup makes a permission or LSM call, so a path sus_path answers ENOENT for still yields a
 * usable descriptor: the fd cannot be read, but it proves the file exists
 * (fstatat(AT_EMPTY_PATH), /proc/self/fd/N, an O_PATH fd as an *at() dirfd, ...), and that is
 * all a detector needs.
 *
 * The only place left to answer is AFTER the kernel has made its own decision, so this layer
 * post-checks: the open runs exactly as the kernel chose to run it, and if the file it
 * produced is one of sus_path's, the descriptor is taken away again and the syscall is
 * answered -ENOENT.  That is why the sys_call_table entries are the mechanism here (see
 * susfs_fp_hook.c for why the table and not an inline hook, and for the ordering rules).
 *
 * Why f_mode and not the flags
 * ----------------------------
 * O_PATH is decided from the file the kernel produced, not from what the caller asked for:
 * do_dentry_open() sets FMODE_PATH for an O_PATH open and for nothing else, so the bit IS the
 * kernel's conclusion.  Reading the flags instead would mean reading user memory - openat2
 * passes a struct open_how, so the flags, the mode and the resolve field all have to be
 * copied in and validated first, and a caller can change them between the copy and the open -
 * and it would have to be done twice, once per ABI (the 32-bit layouts differ).  None of that
 * is needed: by the time this code runs, the question has already been answered.
 *
 * Why the inode and not a path
 * ----------------------------
 * The dentry/inode pair in f_path is what the kernel resolved, under the caller's own root,
 * cwd and mount namespace.  Re-resolving a path string here would be a second lookup that a
 * rename/rmdir/mount in between can point somewhere else - the TOCTOU this whole layer exists
 * to avoid - and the path string is not even available: it would have to be read out of the
 * caller's registers.  sus_path decides by identity, so the inode is the whole input.
 *
 * What this file must NOT do: no path resolution, no user memory access, no namespace work,
 * and no instruction rewriting anywhere (only the data of the table entries - a rewritten
 * .cfi_jt stub was measured as an unrecoverable "Oops - FPAC", see susfs_fp_hook.c).
 */

#include <linux/kernel.h>
#include <linux/version.h>	/* LINUX_VERSION_CODE: close_fd_get_file() changed prototype in 6.1 */
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>		/* struct file, FMODE_PATH */
#include <linux/file.h>		/* fput() */
#include <linux/fdtable.h>	/* files_struct/fdtable for atomic fd rollback */
#include <linux/atomic.h>
#include <linux/cred.h>		/* current_uid(), for the control node */
#include <linux/moduleparam.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/fcntl.h>	/* O_PATH, for the wrapper's flags fast path */
#include <asm/unistd.h>		/* __NR_openat, __NR_openat2, __NR_syscalls */
#include <asm/ptrace.h>		/* struct pt_regs: the wrappers read the caller's argument
				 * registers.  Written out because nothing else here reaches it -
				 * susfs_fp_hook.h only forward-declares the struct, and none of
				 * <linux/fs.h>/<linux/cred.h>/<linux/mm.h> pulls asm/ptrace.h in
				 * (the kprobe layers get it through <linux/kprobes.h>, sus_path.c
				 * through <asm/syscall.h>). */

#include "symbol_resolver.h"
#include "susfs_log.h"
#include "sus_path.h"
#include "susfs_control.h"
#include "susfs_opath.h"
#include "susfs_opath_data.h"
#include "susfs_opath_phys.h"
#include "susfs_fp_hook.h"
#include "susfs_hooks.h"
#include "susfs_or_entry.h"	/* open_redirect's name layer shares exactly these five entries */
#include "ksu_allow_gate.h"	/* the control-node gate is su's own */

/* The AArch32 numbers.  No arm64 header defines them as __NR_*: they are what
 * arch/arm64/include/asm/unistd32.h is generated from, i.e.
 * arch/arm/tools/syscall.tbl:19 (`5  common  open  sys_open`) and :339
 * (`322  common  openat  sys_openat`).  The native numbers come from
 * include/uapi/asm-generic/unistd.h:183 (openat 56) and :854 (openat2 437). */
#define SUSFS_COMPAT_NR_OPEN		5
#define SUSFS_COMPAT_NR_OPENAT		322
/* The AArch32 openat2 slot carries the same number as the native one but is a DIFFERENT array
 * element: arch/arm64/include/asm/unistd32.h:884-885 registers it as
 * `__SYSCALL(__NR_openat2, sys_openat2)` - the NATIVE implementation, not a compat wrapper - and
 * arch/arm64/kernel/sys32.c:130 turns that into
 * `compat_sys_call_table[437] = __arm64_sys_openat2`.  Patching sys_call_table[437] therefore
 * does not patch this element, and a 32-bit task calling openat2(..., O_PATH) would walk past
 * the layer entirely.  It needs its own entry, its own wrapper and its own original slot even
 * though both slots hold the same function. */
#define SUSFS_COMPAT_NR_OPENAT2		437

/* The BTI landing pad an indirect call into this module has to land on.  fp_has_landing_pad()
 * in susfs_fp_hook.c is static and is what susfs_fp_install() refuses a wrapper with; the same
 * test is written out here so opath_dump can report it on the device for a wrapper that is not
 * installed at all - a dump whose answer only exists when the hook is already live would be
 * useless for the step it is meant for. */
#define SUSFS_ARM64_BTI_C	0xd503245fu

static inline bool opath_has_landing_pad(const void *fn)
{
	return fn && READ_ONCE(*(const u32 *)fn) == SUSFS_ARM64_BTI_C;
}
/* ---- /proc/susfs_opath ---- */
static int opath_proc_show(struct seq_file *m, void *v);
static int opath_proc_open(struct inode *inode, struct file *file);

static const struct proc_ops opath_proc_ops = {
	.proc_open = opath_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static struct proc_dir_entry *opath_proc_entry;

static int opath_proc_show(struct seq_file *m, void *v)
{
	int i;

	for (i = 0; i < (int)opath_hooks_count; i++) {
		struct susfs_fp_hook *h = &opath_hooks[i];

		seq_printf(m, "[%d] %s nr=%d nr_compat=%d installed=%d wrapper=%px orig=%px orig_compat=%px\n",
			   i + 1, h->name, h->nr, h->nr_compat, (int)h->installed,
			   (void *)h->wrapper, (void *)h->orig, (void *)h->orig_compat);
	}

	/* No "armed" flag of its own: `installed` above is the state, one line per entry.  The
	 * counters are what a bring-up is judged by - seen without opath means the wrapper runs
	 * and every open it sees is not an O_PATH one, and hidden without deny would mean the fd
	 * was not taken away. */
	seq_printf(m, "seen=%d opath=%d hidden=%d deny=%d err=%d (opath_test=%d opath_dump=%d)\n",
		   atomic_read(&n_opath_seen), atomic_read(&n_opath_opath),
		   atomic_read(&n_opath_hidden), atomic_read(&n_opath_deny),
		   atomic_read(&n_opath_err), opath_test, opath_dump);
	return 0;
}

static int opath_proc_open(struct inode *inode, struct file *file)
{
	/* Root-only, with 0777 on the node on purpose: DAC has to pass so that sus_path's LSM
	 * layer is the only thing answering a non-root reader (ENOENT), see susfs_main.c and
	 * susfs_kstat_init().  Created only when that layer is up (susfs_control_node_allowed). */
	if (!susfs_control_visible())
		return -ENOENT;
	return single_open(file, opath_proc_show, NULL);
}

int susfs_opath_init(void)
{
	int i, attempted = 0, armed = 0, err;

	if (opath_exiting) {
		pr_warn("opath: init while the layer is being torn down - not arming\n");
		return -ENODEV;
	}

	opath_fget_raw = (void *)find_kernel_symbol_exact("fget_raw");

	/* Which close-fd helper this kernel has, under WHICH prototype.  close_fd_get_file() kept its
	 * name across 6.1 and changed its signature there, so the prototype comes from the kernel
	 * this variant is built for (see the note on the pointers above) and the name is resolved
	 * into it.  file_close_fd() exists only where the old name does not, so it is tried after
	 * it; resolving by name - rather than assuming a version's name set - is what still covers
	 * a vendor backport of either helper. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
	opath_close_fd = (void *)find_kernel_symbol_exact("close_fd_get_file");
	opath_close_fd_name = "close_fd_get_file";
	if (!opath_close_fd) {
		opath_close_fd = (void *)find_kernel_symbol_exact("file_close_fd");
		opath_close_fd_name = "file_close_fd";
	}
#else
	opath_close_fd_get_file = (void *)find_kernel_symbol_exact("close_fd_get_file");
	opath_close_fd_name = "close_fd_get_file";
	/* Its result owns two references: the fd table's, and the one get_file() added. */
	opath_close_fd_extra_ref = true;
#endif

	if (!opath_fget_raw || !opath_close_fd_resolved()) {
		/* Refusing to install beats installing a wrapper that cannot finish: without these the
		 * post-check could not take a descriptor away, so the layer would only have added a fault
		 * path to every open on the system.  Every name is printed, because "the name is missing"
		 * is only useful with which one. */
		pr_warn("opath: fget_raw=%px close_fd_get_file=%px file_close_fd=%px - a name was not resolved, no table entry is installed and O_PATH stays visible\n",
			(void *)opath_fget_raw,
			(void *)find_kernel_symbol_exact("close_fd_get_file"),
			(void *)find_kernel_symbol_exact("file_close_fd"));
		atomic_inc(&n_opath_err);
		return -ENOENT;
	}

	err = susfs_fp_init();
	if (err) {
		pr_warn("opath: susfs_fp_init failed %d - the layer stays off\n", err);
		return err;
	}

	if (opath_dump) {
		susfs_opath_dump_all();
		/* Nothing is installed on purpose, and the layer is still "armed" for the
		 * layer table: this mode exists to read the device's own answers (entry
		 * values, symbols, the landing pad) before any entry is replaced. */
		SUSFS_LOGI("opath: opath_dump=%d - diagnostics only, nothing installed\n", opath_dump);
		return 0;
	}

	for (i = 0; i < (int)opath_hooks_count; i++) {
		struct susfs_fp_hook *h = &opath_hooks[i];

		if (opath_test && opath_test != i + 1) {
			SUSFS_LOGI("opath: [%d] %s skipped (opath_test=%d)\n",
				i + 1, h->name, opath_test);
			continue;
		}
		attempted++;

		/* One entry at a time and never fatal for the others: the remaining entries
		 * still answer the kernel's own way, which is what makes a first bring-up
		 * readable instead of all-or-nothing. */
		opath_reg[i].owner = "opath";
		opath_reg[i].name = h->name;
		opath_reg[i].target = SUSFS_HOOK_TARGET_FP;
		opath_reg[i].fp = h;
		err = susfs_hook_register(&opath_reg[i]);
		if (err < 0) {
			pr_warn("opath: [%d] %s not installed (%d) - that ABI stays visible\n",
				i + 1, h->name, err);
			continue;
		}
		armed++;
	}

	if (!attempted) {
		pr_warn("opath: opath_test=%d selects none of the %u entries - the layer stays off\n",
			opath_test, (unsigned int)opath_hooks_count);
		return -EINVAL;
	}
	if (!armed) {
		/* Non-zero so the layer table records "not armed"; the caller's exit is
		 * idempotent, so the entries that did go in are taken back out. */
		pr_warn("opath: none of the %d selected table entries could be installed - O_PATH stays visible\n",
			attempted);
		return -ENOENT;
	}
	if (armed < attempted)
		pr_warn("opath: %d of %d selected table entries installed - O_PATH is answered only for those (the log above names the rest)\n",
			armed, attempted);


	if (susfs_control_node_allowed()) {
		opath_proc_entry = proc_create("susfs_opath", 0777, NULL, &opath_proc_ops);
		if (!opath_proc_entry)
			pr_warn("opath: proc_create(susfs_opath) failed\n");
	} else {
		SUSFS_LOGI("opath: /proc node not created (expose_proc=%d lsm=%d)\n",
			(int)susfs_expose_proc, (int)sus_path_lsm_active());
	}

	SUSFS_LOGI("opath armed: %d/%d entries, proc=%d, fget_raw=%px %s=%px extra_ref=%d\n",
		armed, attempted, opath_proc_entry != NULL,
		(void *)opath_fget_raw, opath_close_fd_name,
		opath_close_fd_pointer(), (int)opath_close_fd_extra_ref);
	return 0;
}

void susfs_opath_exit(void)
{
	int i;

	/* Idempotent: the flag is what makes it so, and it matters because this exit runs for
	 * BOTH paths that can reach it - the rollback of a failed load and the unload of a
	 * loaded module - and the drain below is a synchronize_rcu_tasks() that must not be
	 * repeated for no reason.
	 *
	 * No lock around the flag, and none of the *_exiting-plus-mutex discipline
	 * susfs_open_redirect.c needs: there, or_register() is reachable from a /proc write
	 * whose fd holds no module reference, so a write already inside the handler can re-arm
	 * a probe after the module has been freed.  This layer has no such path - its only arm
	 * entry point is susfs_opath_init(), reached from susfs_init() before the module is
	 * reachable by any caller and never again: no supercall, no /proc write and no other
	 * layer installs a table entry here.  The flag is kept so the invariant is stated in
	 * code rather than in a comment, and so a future arm path has to be added on purpose. */
	if (opath_exiting)
		return;
	opath_exiting = true;

	/* Before the entries come out: the name layer must stop being reachable from them at the same
	 * moment, and susfs_or_entry_exit() (called from open_redirect's exit, which runs first because
	 * the layer table is torn down in reverse) has already cleared its hook pointer. */

	for (i = 0; i < (int)opath_hooks_count; i++) {
		if (opath_reg[i].backend == SUSFS_HOOK_BACKEND_NONE)
			continue;
		susfs_hook_unregister(&opath_reg[i]);
	}

	/* AFTER every entry is restored, never before: this waits until no CPU can still be
	 * inside a wrapper, and an entry that still pointed at one of them would send the next
	 * caller into module text the kernel is about to free.  Restoring the entries stops new
	 * callers; only the drain covers the caller that already picked the wrapper out of the
	 * table. */
	susfs_fp_drain();

	if (opath_proc_entry) {
		proc_remove(opath_proc_entry);
		opath_proc_entry = NULL;
	}

	SUSFS_LOGI("opath disarmed: seen=%d opath=%d hidden=%d deny=%d err=%d\n",
		atomic_read(&n_opath_seen), atomic_read(&n_opath_opath),
		atomic_read(&n_opath_hidden), atomic_read(&n_opath_deny),
		atomic_read(&n_opath_err));
}
