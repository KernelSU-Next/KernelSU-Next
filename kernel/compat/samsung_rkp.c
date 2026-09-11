#include <asm/syscall.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/task_work.h>

#include "arch.h"
#include "compat/samsung_rkp.h"
#include "feature/sucompat.h"
#include "hook/setuid_hook.h"
#include "hook/syscall_event_bridge.h"
#include "hook/syscall_hook.h"
#include "klog.h"
#include "policy/allowlist.h"

#if defined(CONFIG_KSU_SAMSUNG_RKP) && defined(CONFIG_KRETPROBES) &&           \
	defined(__aarch64__)
#define SAMSUNG_RKP_BYPASS_NR (-2)

struct samsung_setresuid_work {
	struct callback_head callback;
	uid_t old_uid;
	uid_t new_uid;
};

static bool hooks_registered;
static bool setresuid_registered;

static bool consume_bypass(int syscall_nr)
{
	struct pt_regs *regs = task_pt_regs(current);

	if (unlikely(regs->syscallno == SAMSUNG_RKP_BYPASS_NR)) {
		regs->syscallno = syscall_nr;
		return true;
	}
	return false;
}

static long __nocfi samsung_execve(const struct pt_regs *regs)
{
	struct pt_regs *syscall_regs = (struct pt_regs *)regs;
	long ret;

	syscall_regs->syscallno = SAMSUNG_RKP_BYPASS_NR;
	ret = ksu_hook_execve(__NR_execve, regs);
	syscall_regs->syscallno = __NR_execve;
	return ret;
}

static long __nocfi samsung_execveat(const struct pt_regs *regs)
{
	struct pt_regs *syscall_regs = (struct pt_regs *)regs;
	long ret;

	syscall_regs->syscallno = SAMSUNG_RKP_BYPASS_NR;
	ret = ksu_hook_execveat(__NR_execveat, regs);
	syscall_regs->syscallno = __NR_execveat;
	return ret;
}

static long __nocfi samsung_newfstatat(const struct pt_regs *regs)
{
	struct pt_regs *syscall_regs = (struct pt_regs *)regs;
	long ret;

	syscall_regs->syscallno = SAMSUNG_RKP_BYPASS_NR;
	ret = ksu_hook_newfstatat(__NR_newfstatat, regs);
	syscall_regs->syscallno = __NR_newfstatat;
	return ret;
}

static long __nocfi samsung_faccessat(const struct pt_regs *regs)
{
	struct pt_regs *syscall_regs = (struct pt_regs *)regs;
	long ret;

	syscall_regs->syscallno = SAMSUNG_RKP_BYPASS_NR;
	ret = ksu_hook_faccessat(__NR_faccessat, regs);
	syscall_regs->syscallno = __NR_faccessat;
	return ret;
}

static int execve_pre(struct kprobe *probe, struct pt_regs *regs)
{
	(void)probe;
	if (consume_bypass(__NR_execve))
		return 0;
	instruction_pointer_set(regs, (unsigned long)samsung_execve);
	return 1;
}

static int execveat_pre(struct kprobe *probe, struct pt_regs *regs)
{
	(void)probe;
	if (consume_bypass(__NR_execveat))
		return 0;
	instruction_pointer_set(regs, (unsigned long)samsung_execveat);
	return 1;
}

static int newfstatat_pre(struct kprobe *probe, struct pt_regs *regs)
{
	(void)probe;
	if (consume_bypass(__NR_newfstatat))
		return 0;
	if (!READ_ONCE(ksu_su_compat_enabled) ||
	    !ksu_is_allow_uid_for_current(current_uid().val))
		return 0;
	instruction_pointer_set(regs, (unsigned long)samsung_newfstatat);
	return 1;
}

static int faccessat_pre(struct kprobe *probe, struct pt_regs *regs)
{
	(void)probe;
	if (consume_bypass(__NR_faccessat))
		return 0;
	if (!READ_ONCE(ksu_su_compat_enabled) ||
	    !ksu_is_allow_uid_for_current(current_uid().val))
		return 0;
	instruction_pointer_set(regs, (unsigned long)samsung_faccessat);
	return 1;
}

static struct kprobe execve_kprobe = { .pre_handler = execve_pre };
static struct kprobe execveat_kprobe = { .pre_handler = execveat_pre };
static struct kprobe newfstatat_kprobe = { .pre_handler = newfstatat_pre };
static struct kprobe faccessat_kprobe = { .pre_handler = faccessat_pre };

static void setresuid_work(struct callback_head *callback)
{
	struct samsung_setresuid_work *work =
		container_of(callback, struct samsung_setresuid_work, callback);

	ksu_handle_setresuid(work->old_uid, work->new_uid);
	kfree(work);
}

static int setresuid_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	(void)regs;
	*(uid_t *)ri->data = current_uid().val;
	return 0;
}

static int setresuid_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct samsung_setresuid_work *work;
	uid_t old_uid = *(uid_t *)ri->data;
	uid_t new_uid;

	if (regs_return_value(regs) < 0)
		return 0;
	new_uid = current_uid().val;
	if (old_uid == new_uid)
		return 0;

	work = kzalloc(sizeof(*work), GFP_ATOMIC);
	if (!work)
		return 0;
	work->old_uid = old_uid;
	work->new_uid = new_uid;
	work->callback.func = setresuid_work;
	if (task_work_add(current, &work->callback, TWA_RESUME))
		kfree(work);
	return 0;
}

static struct kretprobe setresuid_kretprobe = {
	.entry_handler = setresuid_entry,
	.handler = setresuid_return,
	.data_size = sizeof(uid_t),
};

static int register_syscall_kprobe(struct kprobe *probe, int nr)
{
	probe->addr = (kprobe_opcode_t *)READ_ONCE(ksu_syscall_table[nr]);
	return register_kprobe(probe);
}

static void unregister_syscall_hooks(void)
{
	if (!hooks_registered)
		return;
	unregister_kprobe(&faccessat_kprobe);
	unregister_kprobe(&newfstatat_kprobe);
	unregister_kprobe(&execveat_kprobe);
	unregister_kprobe(&execve_kprobe);
	hooks_registered = false;
}
#endif

int ksu_samsung_rkp_init(void)
{
#if defined(CONFIG_KSU_SAMSUNG_RKP) && defined(CONFIG_KRETPROBES) &&           \
	defined(__aarch64__)
	int ret;

	if (!ksu_syscall_table)
		return -ENOENT;
	ret = register_syscall_kprobe(&execve_kprobe, __NR_execve);
	if (ret)
		return ret;
	ret = register_syscall_kprobe(&execveat_kprobe, __NR_execveat);
	if (ret)
		goto unregister_execve;
	ret = register_syscall_kprobe(&newfstatat_kprobe, __NR_newfstatat);
	if (ret)
		goto unregister_execveat;
	ret = register_syscall_kprobe(&faccessat_kprobe, __NR_faccessat);
	if (ret)
		goto unregister_newfstatat;
	hooks_registered = true;

	setresuid_kretprobe.kp.addr =
		(kprobe_opcode_t *)READ_ONCE(ksu_syscall_table[__NR_setresuid]);
	ret = register_kretprobe(&setresuid_kretprobe);
	if (ret)
		goto unregister_all;
	setresuid_registered = true;
	pr_info("Samsung RKP syscall fallback enabled\n");
	return 0;

unregister_all:
	unregister_syscall_hooks();
	return ret;
unregister_newfstatat:
	unregister_kprobe(&newfstatat_kprobe);
unregister_execveat:
	unregister_kprobe(&execveat_kprobe);
unregister_execve:
	unregister_kprobe(&execve_kprobe);
	return ret;
#else
	return -EOPNOTSUPP;
#endif
}

void ksu_samsung_rkp_exit(void)
{
#if defined(CONFIG_KSU_SAMSUNG_RKP) && defined(CONFIG_KRETPROBES) &&           \
	defined(__aarch64__)
	if (setresuid_registered) {
		unregister_kretprobe(&setresuid_kretprobe);
		setresuid_registered = false;
	}
	unregister_syscall_hooks();
#endif
}
