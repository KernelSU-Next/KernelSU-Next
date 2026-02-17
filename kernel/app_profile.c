#include <linux/version.h>
#include <linux/capability.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/proc_ns.h>
#include <linux/pid.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
#include <linux/sched/signal.h> // signal_struct
#include <linux/sched/task.h>
#endif
#include <linux/sched.h>
#include <linux/seccomp.h>
#include <linux/slab.h>
#include <linux/thread_info.h>
#include <linux/uidgid.h>
#include <linux/syscalls.h>

#include "allowlist.h"
#include "app_profile.h"
#include "arch.h"
#include "kernel_compat.h"
#include "klog.h" // IWYU pragma: keep
#include "selinux/selinux.h"
#include "su_mount_ns.h"
#include "syscall_hook_manager.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 7, 0)
static struct group_info root_groups = { .usage = REFCOUNT_INIT(2) };
#else
static struct group_info root_groups = { .usage = ATOMIC_INIT(2) };
#endif

void setup_groups(struct root_profile *profile, struct cred *cred)
{
	if (profile->groups_count > KSU_MAX_GROUPS) {
		pr_warn("Failed to setgroups, too large group: %d!\n", profile->uid);
		return;
	}

	if (profile->groups_count == 1 && profile->groups[0] == 0) {
		// setgroup to root and return early.
		if (cred->group_info)
			put_group_info(cred->group_info);
		cred->group_info = get_group_info(&root_groups);
		return;
	}

	u32 ngroups = profile->groups_count;
	struct group_info *group_info = groups_alloc(ngroups);
	if (!group_info) {
		pr_warn("Failed to setgroups, ENOMEM for: %d\n", profile->uid);
		return;
	}

	int i;
	for (i = 0; i < ngroups; i++) {
		gid_t gid = profile->groups[i];
		kgid_t kgid = make_kgid(current_user_ns(), gid);
		if (!gid_valid(kgid)) {
			pr_warn("Failed to setgroups, invalid gid: %d\n", gid);
			put_group_info(group_info);
			return;
		}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0)
		group_info->gid[i] = kgid;
#else
		GROUP_AT(group_info, i) = kgid;
#endif
	}

	groups_sort(group_info);
	set_groups(cred, group_info);
	put_group_info(group_info);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
extern long SYS_SETNS_SYMBOL(const struct pt_regs *regs);
static long ksu_sys_setns(int fd, int flags)
{
	struct pt_regs regs;
	memset(&regs, 0, sizeof(regs));

	PT_REGS_PARM1(&regs) = fd;
	PT_REGS_PARM2(&regs) = flags;

	// TODO: arm support
#if (defined(__aarch64__) || defined(__x86_64__))
	return SYS_SETNS_SYMBOL(&regs);
#else
	return -ENOSYS;
#endif
}
#else
static long ksu_sys_setns(int fd, int flags)
{
	return sys_setns(fd, flags);
}

__weak int ksys_unshare(unsigned long unshare_flags)
{
	return sys_unshare(unshare_flags);
}
#endif

static void setup_mount_namespace(int32_t ns_mode)
{
	pr_info("setup mount namespace for pid: %d\n", current->pid);

	if (ns_mode == 0) {
		pr_info("mount namespace mode: inherit\n");
		// do nothing
		return;
	}

	if (ns_mode > 2) {
		pr_warn("unknown mount namespace mode: %d\n", ns_mode);
		return;
	}

	const struct cred *old_cred = NULL;
	struct cred *new_cred = NULL;
	if (!(capable(CAP_SYS_ADMIN) && capable(CAP_SYS_CHROOT))) {
		pr_info("process dont have CAP_SYS_ADMIN or CAP_SYS_CHROOT, adding it temporarily.\n");
		new_cred = prepare_creds();
		if (!new_cred) {
			pr_warn("failed to prepare new credentials\n");
			return;
		}
		cap_raise(new_cred->cap_effective, CAP_SYS_ADMIN);
		cap_raise(new_cred->cap_effective, CAP_SYS_CHROOT);
		old_cred = override_creds(new_cred);
	}

	if (ns_mode == 1) {
		pr_info("mount namespace mode: global\n");
		struct file *ns_file;
		struct path ns_path;
		struct task_struct *pid1_task = NULL;
		struct pid *pid_struct = NULL;
		rcu_read_lock();
		// find init
		pid_struct = find_pid_ns(1, &init_pid_ns);
		if (unlikely(!pid_struct)) {
			rcu_read_unlock();
			pr_warn("failed to find pid_struct for PID 1\n");
			goto try_drop_caps;
		}

		pid1_task = get_pid_task(pid_struct, PIDTYPE_PID);
		rcu_read_unlock();
		if (unlikely(!pid1_task)) {
			pr_warn("failed to get task_struct for PID 1\n");
			goto try_drop_caps;
		}
		// mabe you can use &init_task for first stage init?
		long ret = ns_get_path(&ns_path, pid1_task, &mntns_operations);
		put_task_struct(pid1_task);
		if (ret) {
			pr_warn("failed to get path for init's mount namespace: %ld\n",
				ret);
			goto try_drop_caps;
		}
		ns_file = dentry_open(&ns_path, O_RDONLY, current_cred());

		path_put(&ns_path);
		if (IS_ERR(ns_file)) {
			pr_warn("failed to open file for init's mount namespace: %ld\n",
				PTR_ERR(ns_file));
			goto try_drop_caps;
		}

		int fd = get_unused_fd_flags(O_CLOEXEC);
		if (fd < 0) {
			pr_warn("failed to get an unused fd: %d\n", fd);
			fput(ns_file);
			goto try_drop_caps;
		}

		fd_install(fd, ns_file);
		pr_info("calling sys_setns with fd : %d\n", fd);

		ret = ksu_sys_setns(fd, CLONE_NEWNS);
		if (ret) {
			pr_warn("sys_setns failed: %ld\n", ret);
		}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
		close_fd(fd);
#else
		__close_fd(current->files, fd);
#endif
	}

	if (ns_mode == 2) {
		long ret;
		pr_info("mount namespace mode: independent\n");

		ret = ksys_unshare(CLONE_NEWNS);
		if (ret) {
			pr_warn("call ksys_unshare failed: %ld\n", ret);
		}
	}

try_drop_caps:
	if (old_cred) {
		pr_info("dropping temporarily capability.\n");
		revert_creds(old_cred);
		put_cred(new_cred);
	}
	return;
}

void seccomp_filter_release(struct task_struct *tsk);

void disable_seccomp(void)
{
	// https://github.com/backslashxx/KernelSU/tree/e28930645e764b9f0e5d0d1b0d5e236464939075/kernel/app_profile.c
	if (!!!current->seccomp.mode) {
		return;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0) ||                          \
     defined(KSU_OPTIONAL_SECCOMP_FILTER_RELEASE))
	struct task_struct *fake;
	fake = kmalloc(sizeof(*fake), GFP_ATOMIC);
	if (!fake) {
		pr_err("%s: cannot allocate fake struct!\n", __func__);
		return;
	}
#endif

    // Refer to kernel/seccomp.c: seccomp_set_mode_strict
    // When disabling Seccomp, ensure that current->sighand->siglock is held during the operation.
    spin_lock_irq(&current->sighand->siglock);
	// disable seccomp
#if defined(CONFIG_GENERIC_ENTRY) &&                                           \
	LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
	clear_syscall_work(SECCOMP);
#else
	clear_thread_flag(TIF_SECCOMP);
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0) ||                          \
     defined(KSU_OPTIONAL_SECCOMP_FILTER_RELEASE))
	memcpy(fake, current, sizeof(*fake));
	atomic_set(&current->seccomp.filter_count, 0);
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0) &&                           \
     !defined(KSU_OPTIONAL_SECCOMP_FILTER_RELEASE))
	// put_seccomp_filter is allowed while we holding sighand
	put_seccomp_filter(current);
#endif
	current->seccomp.mode = 0;
	current->seccomp.filter = NULL;
	
    spin_unlock_irq(&current->sighand->siglock);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0) ||                          \
     defined(KSU_OPTIONAL_SECCOMP_FILTER_RELEASE))
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
    // https://github.com/torvalds/linux/commit/bfafe5efa9754ebc991750da0bcca2a6694f3ed3#diff-45eb79a57536d8eccfc1436932f093eb5c0b60d9361c39edb46581ad313e8987R576-R577
    fake->flags |= PF_EXITING;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
    // https://github.com/torvalds/linux/commit/0d8315dddd2899f519fe1ca3d4d5cdaf44ea421e#diff-45eb79a57536d8eccfc1436932f093eb5c0b60d9361c39edb46581ad313e8987R556-R558
    fake->sighand = NULL;
#endif
	seccomp_filter_release(fake);
	kfree(fake);
#endif
}

void escape_with_root_profile(void)
{
	struct cred *cred;

	cred = prepare_creds();
	if (!cred) {
		pr_warn("prepare_creds failed!\n");
		return;
	}

	if (cred->euid.val == 0) {
		pr_warn("Already root, don't escape!\n");
		abort_creds(cred);
		return;
	}

	struct root_profile *profile = ksu_get_root_profile(cred->uid.val);

	cred->uid.val = profile->uid;
	cred->suid.val = profile->uid;
	cred->euid.val = profile->uid;
	cred->fsuid.val = profile->uid;

	cred->gid.val = profile->gid;
	cred->fsgid.val = profile->gid;
	cred->sgid.val = profile->gid;
	cred->egid.val = profile->gid;
	cred->securebits = 0;

	BUILD_BUG_ON(sizeof(profile->capabilities.effective) !=
		     sizeof(kernel_cap_t));

	// setup capabilities
	// we need CAP_DAC_READ_SEARCH becuase `/data/adb/ksud` is not accessible for non root process
	// we add it here but don't add it to cap_inhertiable, it would be dropped automaticly after exec!
	u64 cap_for_ksud = profile->capabilities.effective | CAP_DAC_READ_SEARCH;
	memcpy(&cred->cap_effective, &cap_for_ksud, sizeof(cred->cap_effective));
	memcpy(&cred->cap_permitted, &profile->capabilities.effective,
	       sizeof(cred->cap_permitted));
	memcpy(&cred->cap_bset, &profile->capabilities.effective,
	       sizeof(cred->cap_bset));

	setup_groups(profile, cred);
	setup_selinux(profile->selinux_domain, cred);

	commit_creds(cred);

	disable_seccomp();

#ifdef KSU_KPROBES_HOOK
	struct task_struct *p = current;
	struct task_struct *t;
	for_each_thread (p, t) {
		ksu_set_task_tracepoint_flag(t);
	}
#endif

	setup_mount_ns(profile->namespaces);
}

void escape_to_root_for_init(void) {
	struct cred *cred = prepare_creds();
    if (!cred) {
        pr_err("Failed to prepare init's creds!\n");
        return;
    }

    setup_selinux(KERNEL_SU_CONTEXT, cred);
    commit_creds(cred);
}
