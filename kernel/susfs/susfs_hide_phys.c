/* SPDX-License-Identifier: GPL-2.0-only */
/* Kernel layouts, table identity, probe register adapters and lifecycle. */
#include <linux/module.h>
#include <linux/version.h>
#include <linux/kprobes.h>
#include <linux/seq_file.h>
#include <linux/kallsyms.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include "susfs_log.h"
#include "sus_path.h"
#include "symbol_resolver.h"
#include "susfs_kprobe.h"
#include "susfs_hook_engine.h"
#include "susfs_hooks.h"
#include "susfs_hide_data.h"
#include "susfs_hide_policy.h"
#include "susfs_hide_phys.h"

void hide_modules_sync_sysfs(void)
{
	char path[64];
	int i;

	/* sus_path_del_path() is a no-op for a name that was never registered. */
	for (i = 0; i < n_hide_modules_applied; i++) {
		snprintf(path, sizeof(path), "/sys/module/%s", hide_modules_applied[i]);
		sus_path_del_path(path);
	}
	n_hide_modules_applied = 0;

	for (i = 0; i < n_hide_modules; i++) {
		int rc;

		snprintf(path, sizeof(path), "/sys/module/%s", hide_modules[i]);
		rc = sus_path_add_self_hidden(path);
		if (rc) {
			/* Not fatal: the /proc/modules line is filtered with or without a sysfs directory (-ENOENT: not loaded yet). */
			atomic_inc(&n_sysfs_rules_failed);
			SUSFS_LOGI("hide_modules: %s: no sus_path rule (%d%s)\n", path, rc,
				rc == -ENOENT ? " - not loaded, so its sysfs directory does not exist yet" : "");
			continue;
		}
		strscpy(hide_modules_applied[n_hide_modules_applied++],
			hide_modules[i], MODULE_NAME_LEN);
		atomic_inc(&n_sysfs_rules_added);
	}
}

/* Local mirror of kernel/kallsyms.c struct kallsym_iter; only the name field matters here.
 *
 * It is a MIRROR, so the compiler cannot check it against the kernel's own struct, and the layout
 * is not KMI-frozen across the versions this module builds against: v6.5 removed `pos_arch_end`
 * (measured against upstream: v5.10/v5.15/v6.1 have it, v6.6 does not).  Getting this wrong
 * shifts `name`/`module_name` by 8 bytes, which makes every prefix and module-name test miss
 * while all the counters still report "armed" - a silent no-op, not a crash.  Hence the gate
 * below for the drift that is known, and the plausibility check in hide_syms_s_show_pre() for
 * the drift that is not. */
struct kallsym_iter_local {
	loff_t pos;
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 5, 0)
	loff_t pos_arch_end;		/* gone in v6.5 */
#endif
	loff_t pos_mod_end;
	loff_t pos_ftrace_mod_end;
	loff_t pos_bpf_end;
	unsigned long value;
	unsigned int nameoff;
	char type;
	char name[KSYM_NAME_LEN];
	char module_name[MODULE_NAME_LEN];
	int exported;
	int show_value;
};

static const struct seq_operations *kallsyms_op_target;
static const struct seq_operations *modules_op_target;
static bool hide_syms_iter_plausible(const struct kallsym_iter_local *it)
{
	if (!it->type || !strchr("aAbBdDgGijNnRrSsTtUuVvWw", it->type))
		return false;
	if (strnlen(it->name, sizeof(it->name)) >= sizeof(it->name))
		return false;
	/* Empty for a core-kernel symbol, a short module name otherwise - never unterminated. */
	return strnlen(it->module_name, sizeof(it->module_name)) < sizeof(it->module_name);
}

static unsigned long hide_s_addr, hide_m_addr;
static bool hide_registered, m_show_registered;
static bool hide_s_should_hide(struct seq_file *m)
{
	struct kallsym_iter_local *iter;

	atomic_inc(&hide_enter_count);
	if (!m || (kallsyms_op_target && m->op != kallsyms_op_target) || !m->private)
		return false;
	iter = (struct kallsym_iter_local *)m->private;
	if (!hide_syms_iter_plausible(iter) || !iter->name[0])
		return false;
	return susfs_hide_symbol_decide(iter->name, iter->module_name);
}

static bool hide_m_should_hide(struct seq_file *m, void *p)
{
	struct module *mod;

	if (!m || (modules_op_target && m->op != modules_op_target) || !p)
		return false;
	mod = (struct module *)((char *)p - offsetof(struct module, list));
	return susfs_hide_module_decide(mod->name);
}

int __nocfi hide_syms_s_show_decide(struct seq_file *m, void *p)
{
	return hide_s_should_hide(m) ? 1 : 0;
}

int __nocfi hide_syms_m_show_decide(struct seq_file *m, void *p)
{
	return hide_m_should_hide(m, p) ? 1 : 0;
}

static int hide_syms_s_show_pre(struct kprobe *kp, struct pt_regs *regs)
{
	struct seq_file *m = (struct seq_file *)regs_get_kernel_argument(regs, 0);
	struct kallsym_iter_local *iter;

	atomic_inc(&hide_enter_count);
	/* The identity check applies only when the table was resolved: with kallsyms_op_target NULL
	 * this probe was registered BY NAME (see susfs_hide_syms_init()), and then the gate below is
	 * the only thing between a wrong s_show and the string walk - the pre-identity behaviour. */
	if (!m || (kallsyms_op_target && m->op != kallsyms_op_target) || !m->private)
		return 0;
	iter = (struct kallsym_iter_local *)m->private;
	if (!hide_syms_iter_plausible(iter)) {
		/* A rejected caller proves neither state: on the by-name path it may be a shared s_show
		 * body (hiding still on) or another s_show (hiding off); with the identity resolved it is
		 * a real mirror drift.  Only the unload counters can settle it. */
		if ((unsigned int)atomic_inc_return(&hide_layout_bad) == 1u) {
			if (kallsyms_op_target)
				pr_err("susfs_hide_syms: kallsyms m->private is not a struct kallsym_iter - the mirror drifted; /proc/kallsyms is NOT being filtered\n");
			else
				pr_warn("susfs_hide_syms: non-kallsyms caller on the by-name path (m->private is not a struct kallsym_iter) - skipping it; this alone proves neither that hiding is off nor that it is on - check the unload counters\n");
		}
		return 0;
	}
	if (!iter->name[0])
		return 0;

	if (!susfs_hide_symbol_decide(iter->name, iter->module_name))
		return 0;
	regs->regs[0] = 0;
	regs->pc = regs->regs[30];
	return 1;
}

static int hide_syms_m_show_pre(struct kprobe *kp, struct pt_regs *regs)
{
	struct seq_file *m = (struct seq_file *)regs_get_kernel_argument(regs, 0);
	struct module *mod;
	void *p = (void *)regs_get_kernel_argument(regs, 1);

	/* Same rule as the s_show handler: the identity check is in force only when modules_op was
	 * resolved; with modules_op_target NULL the probe was armed BY NAME (see init) and only the
	 * name decides which m_show this is. */
	if (!m || (modules_op_target && m->op != modules_op_target) || !p)
		return 0;

	/* Same recovery the kernel's own m_show() does: the iterator hands over &module->list, so the module is one offset away. */
	mod = (struct module *)((char *)p - offsetof(struct module, list));
	if (!susfs_hide_module_decide(mod->name))
		return 0;

	regs_set_return_value(regs, 0);		/* no output for this entry */
	regs->pc = regs->regs[30];
	return 1;
}

static struct kprobe kp_s_show = { .pre_handler = hide_syms_s_show_pre };
static struct kprobe kp_m_show = { .pre_handler = hide_syms_m_show_pre };

static int __nocfi noinline hide_syms_resolve_show(struct kprobe *kp,
		const char *table_name, const char *symbol_name,
		const struct seq_operations **table_out)
{
	const struct seq_operations *op;
	unsigned long addr = find_kernel_symbol_exact(table_name);
	unsigned long show, target, pc;
	u32 insn;

	*table_out = NULL;
	if (!addr)
		return -ENOENT;
	op = (const struct seq_operations *)addr;
	if (copy_from_kernel_nofault(&show, &op->show, sizeof(show)))
		return -EFAULT;
	if (!show || (show & 3))
		return -EINVAL;

	target = show;
	if (!ksu_symbol_is_start(show, symbol_name)) {

		pc = show;
		if (copy_from_kernel_nofault(&insn, (void *)pc, sizeof(insn)))
			return -EFAULT;
		if (insn == 0xd503245f) {		/* bti c: the entry stub's landing pad */
			pc += sizeof(insn);
			if (copy_from_kernel_nofault(&insn, (void *)pc, sizeof(insn)))
				return -EFAULT;
		}
		if ((insn & 0xfc000000) != 0x14000000)	/* not an unconditional branch: no jump to follow */
			return -EINVAL;
		target = pc + (long)((s32)(insn << 6) >> 4);
		if (!target || (target & 3) || !ksu_symbol_is_start(target, symbol_name))
			return -EINVAL;
		SUSFS_LOGI("susfs_hide_syms: %s.show=%px is an entry stub - body at %px\n",
				table_name, (void *)show, (void *)target);
	}

	/* The indirect call that reaches this function is BTI-checked.  A kprobe
	 * breakpoint placed on the first instruction would overwrite the `bti c`
	 * landing pad and turn every seq_read() of the file into a BTI fault.
	 * Keep the landing pad and probe the instruction after it. */
	if (!copy_from_kernel_nofault(&insn, (void *)target, sizeof(insn)) &&
	    insn == 0xd503245f) {
		target += sizeof(insn);
		SUSFS_LOGI("susfs_hide_syms: %s.show BTI landing pad kept, probing %px\n",
				table_name, (void *)target);
	}
	kp->addr = (kprobe_opcode_t *)target;
	*table_out = op;
	SUSFS_LOGI("susfs_hide_syms: %s.show=%px, probe body=%px\n",
			table_name, (void *)show, (void *)target);
	return 0;
}

int susfs_hide_phys_init(struct susfs_hook *s_hook, struct susfs_hook *m_hook)
{
	int rc;

	/* No retry path exists for this layer today (it is armed once, from the layer table).  If one
	 * is ever added it MUST re-run hide_syms_resolve_show() instead of calling register_kprobe()
	 * again: on the identity path susfs_kp_forget_addr() leaves neither .addr nor .symbol_name
	 * set, and the kernel answers -EINVAL for that combination (see susfs_kprobe.h). */
	rc = hide_syms_resolve_show(&kp_s_show, "kallsyms_op", "s_show",
			&kallsyms_op_target);
	if (rc) {
		/* Identity resolution failed: the table is not there, or its .show is not the body of a
		 * function named s_show (see hide_syms_resolve_show()).  Register BY NAME instead of
		 * hiding nothing - the name-based path was measured to work on a device where the table
		 * check does not.  kallsyms_op_target stays NULL, which is how the handler knows the
		 * m->op identity check is off and that hide_syms_iter_plausible() is the only gate; and
		 * .addr is cleared explicitly so the two fields are never set at once (that pair is what
		 * the kernel refuses with -EINVAL). */
		SUSFS_LOGI("susfs_hide_syms: kallsyms_op.show could not be validated (%d) - arming s_show BY NAME instead: the m->op check is OFF for this probe, so hide_syms_iter_plausible() is the only gate (if the name selects another s_show, that file can lose a line, and kallsyms hiding stays off)\n",
				rc);
		kp_s_show.addr = NULL;
		kp_s_show.symbol_name = "s_show";
	}
	{
		hide_s_addr = (unsigned long)kp_s_show.addr;
		susfs_hook_override("s_show", hide_s_addr);
		rc = susfs_hook_register_assign(s_hook,
						SUSFS_HOOK_BACKEND_INLINE);
		if (rc < 0) {
			pr_warn("susfs_hide_syms: s_show inline failed %d\n", rc);
			return rc;
		}
		hide_registered = true;
		SUSFS_LOGI("susfs_hide_syms: s_show inline at %px (%s)\n",
				(void *)hide_s_addr,
				kallsyms_op_target ? "by table identity" : "BY NAME");
	}
	/* Separate probe, separate failure: hiding symbol names and hiding module entries are independent. */
	rc = hide_syms_resolve_show(&kp_m_show, "modules_op", "m_show",
			&modules_op_target);
	if (rc) {
		/* Fail closed: a name-only fallback would arm an unvalidated function and
		 * cannot tell /proc/modules' m_show from another same-named callback. */
		susfs_kp_forget_addr(&kp_m_show);
		modules_op_target = NULL;
		SUSFS_LOGI("susfs_hide_syms: modules_op.show could not be validated (%d) - NOT arming m_show; /proc/modules keeps listing the hidden modules\n",
				rc);
	} else {
		hide_m_addr = (unsigned long)kp_m_show.addr;
		susfs_hook_override("m_show", hide_m_addr);
		rc = susfs_hook_register_assign(m_hook,
						SUSFS_HOOK_BACKEND_INLINE);
		if (rc < 0) {
			pr_warn("susfs_hide_syms: m_show inline failed %d\n", rc);
		} else {
			m_show_registered = true;
			SUSFS_LOGI("susfs_hide_syms: m_show inline at %px\n", (void *)hide_m_addr);
		}
	}
	return 0;
}

void susfs_hide_phys_exit(struct susfs_hook *s_hook, struct susfs_hook *m_hook)
{
	if (m_show_registered) {
		susfs_hook_unregister(m_hook);
		m_show_registered = false;
	}
	if (hide_registered) {
		susfs_hook_unregister(s_hook);
		hide_registered = false;
	}
	modules_op_target = NULL;
	kallsyms_op_target = NULL;
}

bool susfs_hide_phys_active(void)
{
	return hide_registered;
}
