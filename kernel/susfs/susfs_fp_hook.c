/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_fp_hook.c - intercept syscalls by replacing a sys_call_table entry.
 *
 * Why this and not an inline hook
 * -------------------------------
 * sys_call_table[nr] holds a pointer to the kernel's CFI jump-table stub
 * (__arm64_sys_xxx.cfi_jt), and KernelSU reads that very array when it wants the
 * original behaviour (ksu_syscall_table[orig_nr](regs), see ksu_hook_faccessat).
 * Rewriting the instructions of such a wrapper therefore interferes with every
 * other caller of it - measured on this device as an unrecoverable
 *
 *     Internal error: Oops - FPAC: 0000000072000000
 *     pc : __arm64_sys_faccessat+0x2c4/0x848
 *     lr : ksu_hook_faccessat+0x44/0x58 [kernelsu]
 *
 * Replacing the table entry touches data only, so the whole chain stays as the
 * kernel built it:
 *
 *     invoke_syscall()          -> sys_call_table[nr]        (ours)
 *     sys_enter + dispatcher    -> ksu_syscall_table[nr]     (the same array)
 *     ksu_hook_faccessat()      -> our wrapper
 *     our wrapper (not hidden)  -> saved entry -> .cfi_jt stub -> real wrapper
 *
 * Nothing on that path has a rewritten instruction, so BTI, PAC, the CFI jump
 * table and LTO all remain untouched.
 *
 * The write itself reuses the fixmap primitive that the inline hook layer
 * already brought over from KernelSU/SukiSU (phys_from_virt + FIX_TEXT_POKE0 +
 * copy_to_kernel_nofault + stop_machine + d-cache flush): the table is data in
 * .rodata, so only the d-cache needs maintenance - never the i-cache.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/rcupdate.h>
#include <asm/unistd.h>

#include "patch_memory.h"
#include "symbol_resolver.h"
#include "susfs_log.h"
#include "susfs_kprobe.h"	/* SUSFS_SC_WRAPPER_NAME for the log control symbol */
#include "susfs_fp_hook.h"

/* The generated x86_64 unistd headers of the 5.10 KMIs do not define
 * __NR_syscalls (android13-5.15 and up do); they carry __NR_syscall_max
 * instead, the same fallback kernel/hook/x86_64/syscall_hook.c builds on.
 * arm64 always defines __NR_syscalls, so the guard never fires there. */
#ifndef __NR_syscalls
#define __NR_syscalls (__NR_syscall_max + 1)
#endif

/* The BTI landing pad of an indirect call target: 'bti c' (an unallocated HINT
 * on cores without BTI, so checking for it is correct either way). */
#define SUSFS_ARM64_BTI_C	0xd503245fu
#define SUSFS_ARM64_BTI_JC	0xd50324dfu
#define SUSFS_ARM64_PACIASP	0xd503233fu
#define SUSFS_ARM64_PACIBSP	0xd503237fu

static susfs_syscall_fn_t *sys_call_table_ptr;
static susfs_syscall_fn_t *compat_sys_call_table_ptr;

/* Upper bound for an index into compat_sys_call_table.  It is NOT __NR_syscalls: the 32-bit
 * table is sized __NR_compat_syscalls (arch/arm64/include/asm/unistd.h:41), which exists only
 * with CONFIG_COMPAT - i.e. exactly when the table does.  The native constant is used as the
 * fallback for the check's own sake, never to index with. */
#ifdef __NR_compat_syscalls
#define SUSFS_FP_COMPAT_MAX_NR	__NR_compat_syscalls
#else
#define SUSFS_FP_COMPAT_MAX_NR	__NR_syscalls
#endif

/* The compat index of a hook, -1 when it has no 32-bit entry (nr_compat 0).  A negative value
 * is not a valid index and is refused by the caller, so it never reaches the table. */
static int fp_compat_nr(const struct susfs_fp_hook *h)
{
	return h->nr_compat ? h->nr_compat : -1;
}

/* One table patch, with the same ordering rule for both tables: the wrapper reads
 * its original out of the module slot, so the slot is published first and the
 * write is verified afterwards. */
static int fp_patch_entry(susfs_syscall_fn_t *table, int nr, const char *name,
			  susfs_syscall_fn_t wrapper, susfs_syscall_fn_t old)
{
	int err;

	err = ksu_patch_text(&table[nr], &wrapper, sizeof(wrapper),
			     KSU_PATCH_TEXT_FLUSH_DCACHE);
	if (err) {
		pr_warn("susfs_fp: %s: write failed %d\n", name, err);
		return err;
	}
	if (READ_ONCE(table[nr]) != wrapper) {
		/* A half-written pointer is worse than none: put the old one back. */
		ksu_patch_text(&table[nr], &old, sizeof(old),
			       KSU_PATCH_TEXT_FLUSH_DCACHE);
		pr_warn("susfs_fp: %s: write did not stick, rolled back\n", name);
		return -EIO;
	}
	return 0;
}

int susfs_fp_init(void)
{
	void *direct, *walked;

	if (sys_call_table_ptr)
		return 0;

	/* Both lookups, because they do not cover the same symbols: on 5.15
	 * find_kernel_symbol_exact() is kallsyms_lookup_name(), while
	 * ksu_resolve_symbol_for_functable_hook() also walks kallsyms_on_each_symbol -
	 * which is how KernelSU resolves this exact symbol.  Data symbols such as
	 * sys_call_table are the reason to keep both. */
	direct = (void *)find_kernel_symbol_exact("sys_call_table");
	sys_call_table_ptr = (susfs_syscall_fn_t *)direct;
	if (!sys_call_table_ptr) {
		walked = ksu_resolve_symbol_for_functable_hook("sys_call_table");
		sys_call_table_ptr = (susfs_syscall_fn_t *)walked;
	}

	if (!sys_call_table_ptr) {
		/* Keep the evidence in the log: direct lookup vs the kallsyms walk, on
		 * this symbol and on a known-good one of each kind. */
		pr_warn("susfs_fp: sys_call_table not found: direct=%px walk=%px "
			"(control: jiffies direct=%px, openat walk=%px)\n",
			direct, walked,
			(void *)find_kernel_symbol_exact("jiffies"),
			ksu_resolve_symbol_for_functable_hook(SUSFS_SC_WRAPPER_NAME(openat)));
		return -ENOENT;
	}

	pr_info("susfs_fp: sys_call_table at %px (entry 0 %px)\n",
		sys_call_table_ptr, (void *)READ_ONCE(sys_call_table_ptr[0]));

	/* Optional: only the hooks with a compat index (nr_compat) need it, and a kernel without
	 * the table just means those hooks stay native-only or cannot arm at all. */
	compat_sys_call_table_ptr =
		(susfs_syscall_fn_t *)find_kernel_symbol_exact("compat_sys_call_table");
	if (!compat_sys_call_table_ptr)
		compat_sys_call_table_ptr =
			(susfs_syscall_fn_t *)ksu_resolve_symbol_for_functable_hook("compat_sys_call_table");
	pr_info("susfs_fp: compat_sys_call_table at %px (%s)\n",
		compat_sys_call_table_ptr, compat_sys_call_table_ptr ? "ok" : "absent");
	return 0;
}

unsigned long susfs_fp_syscall_table(void)
{
	return (unsigned long)sys_call_table_ptr;
}

unsigned long susfs_fp_compat_syscall_table(void)
{
	return (unsigned long)compat_sys_call_table_ptr;
}

void susfs_fp_dump_entry(int nr, const char *sym)
{
	char cfi_name[128];
	unsigned long plain, cfi, resolved;

	if (!sys_call_table_ptr || nr < 0 || nr >= __NR_syscalls) {
		pr_warn("susfs_fp: dump: table %d or nr %d unusable\n", !!sys_call_table_ptr, nr);
		return;
	}

	plain = find_kernel_symbol_exact(sym);
	snprintf(cfi_name, sizeof(cfi_name), "%s.cfi_jt", sym);
	cfi = find_kernel_symbol_exact(cfi_name);
	resolved = (unsigned long)ksu_resolve_symbol_for_functable_hook(sym);

	pr_info("susfs_fp: nr %d table=%px plain=%px cfi_jt=%px resolved=%px -> %s\n",
		nr, (void *)READ_ONCE(sys_call_table_ptr[nr]),
		(void *)plain, (void *)cfi, (void *)resolved,
		(READ_ONCE(sys_call_table_ptr[nr]) == (susfs_syscall_fn_t)cfi) ? "cfi_jt match" :
		((READ_ONCE(sys_call_table_ptr[nr]) == (susfs_syscall_fn_t)plain) ? "plain match" : "neither"));
}

static bool fp_has_landing_pad(const void *fn);

void susfs_fp_dump_wrapper(const char *name, const void *fn)
{
	if (!fn) {
		pr_warn("susfs_fp: %s: no wrapper\n", name);
		return;
	}
	pr_info("susfs_fp: %s at %px first insn 0x%08x (%s)\n", name, fn,
		READ_ONCE(*(const u32 *)fn),
		fp_has_landing_pad(fn) ? "CALL LANDING PAD" : "NO LANDING PAD");
}

static bool fp_has_landing_pad(const void *fn)
{
#ifdef __x86_64__
	/* endbr64 under IBT, see SUSFS_LANDING_PAD in susfs_fp_hook.h */
	return READ_ONCE(*(const u32 *)fn) == SUSFS_LANDING_PAD;
#else
	switch (READ_ONCE(*(const u32 *)fn)) {
	case SUSFS_ARM64_BTI_C:
	case SUSFS_ARM64_BTI_JC:
	case SUSFS_ARM64_PACIASP:
	case SUSFS_ARM64_PACIBSP:
		return true;
	default:
		return false;
	}
#endif
}

int susfs_fp_install(struct susfs_fp_hook *h)
{
	susfs_syscall_fn_t old;
	int cnr, err;
	bool native_done = false, compat_done = false;

	if (h->installed)
		return 0;
	if (!sys_call_table_ptr) {
		err = susfs_fp_init();
		if (err)
			return err;
	}
	/* nr < 0 is "no native entry", not a typo (see struct susfs_fp_hook): the AArch32-only
	 * syscalls live in the compat table alone, and arm64's sys_call_table[5] is setxattr. */
	if (h->nr >= __NR_syscalls) {
		pr_warn("susfs_fp: %s: bogus nr %d\n", h->name, h->nr);
		return -EINVAL;
	}
	/* The compat number has its own bound, and a negative one is refused here rather than
	 * treated as "none": only nr_compat == 0 means "this hook has no 32-bit entry". */
	if (h->nr_compat < 0 || h->nr_compat >= SUSFS_FP_COMPAT_MAX_NR) {
		pr_warn("susfs_fp: %s: bogus compat nr %d\n", h->name, h->nr_compat);
		return -EINVAL;
	}
	cnr = fp_compat_nr(h);
	if (h->nr < 0 && cnr < 0) {
		pr_warn("susfs_fp: %s: no native entry and no compat entry\n", h->name);
		return -EINVAL;
	}
	if (!h->wrapper || !fp_has_landing_pad((const void *)h->wrapper)) {
		pr_warn("susfs_fp: %s has no valid call landing pad (0x%08x) - not installed\n",
			h->name, h->wrapper ? READ_ONCE(*(const u32 *)h->wrapper) : 0);
		return -EINVAL;
	}

	if (h->nr >= 0) {
		old = READ_ONCE(sys_call_table_ptr[h->nr]);
		if (!old) {
			pr_warn("susfs_fp: %s: table entry is NULL\n", h->name);
			return -ENOENT;
		}

		/* Publish the original BEFORE the table points at us: the wrapper reads this
		 * slot, so a table entry that is live while the slot is still NULL would jump
		 * to zero on another CPU.  The slot is never cleared again for the same
		 * reason - once the entry is restored, nothing calls the wrapper any more. */
		h->orig = old;
		if (h->orig_slot)
			WRITE_ONCE(*h->orig_slot, old);
		smp_wmb();

		err = fp_patch_entry(sys_call_table_ptr, h->nr, h->name, h->wrapper, old);
		if (err)
			return err;
		native_done = true;
	}

	/* The 32-bit table is a second, independent entry: a compat task never reads
	 * sys_call_table, so an entry that is only patched natively is simply not
	 * there for it - and the compat index is its own number system, never h->nr. */
	if (cnr >= 0) {
		if (!compat_sys_call_table_ptr) {
			pr_warn("susfs_fp: %s: compat_sys_call_table absent, no 32-bit entry\n",
				h->name);
		} else {
			susfs_syscall_fn_t cold = READ_ONCE(compat_sys_call_table_ptr[cnr]);

			if (!cold) {
				pr_warn("susfs_fp: %s: compat entry %d is NULL\n",
					h->name, cnr);
			} else {
				h->orig_compat = cold;
				if (h->orig_slot_compat)
					WRITE_ONCE(*h->orig_slot_compat, cold);
				smp_wmb();
				err = fp_patch_entry(compat_sys_call_table_ptr, cnr, h->name,
						     h->wrapper, cold);
				if (err) {
					/* Keep the two tables consistent rather than half-armed. */
					if (native_done)
						ksu_patch_text(&sys_call_table_ptr[h->nr], &h->orig,
							       sizeof(h->orig), KSU_PATCH_TEXT_FLUSH_DCACHE);
					return err;
				}
				compat_done = true;
				pr_info("susfs_fp: %s: compat entry %d armed (original %px)\n",
					h->name, cnr, (void *)cold);
			}
		}
	}

	if (!native_done && !compat_done) {
		/* Reported as a failure so a caller that counts its armed entries does not
		 * record this one as live - there is nothing in either table. */
		pr_warn("susfs_fp: %s: no entry could be armed\n", h->name);
		return -ENOENT;
	}

	h->installed = true;

	pr_info("susfs_fp: nr %d compat %d (%s) armed: table=%px -> %px, original %px/%px\n",
		h->nr, cnr, h->name, h->nr >= 0 ? (void *)&sys_call_table_ptr[h->nr] : NULL,
		(void *)h->wrapper, (void *)h->orig, (void *)h->orig_compat);
	return 0;
}

void susfs_fp_remove(struct susfs_fp_hook *h)
{
	int cnr, err;

	if (!h->installed || !sys_call_table_ptr)
		return;

	cnr = fp_compat_nr(h);

	if (h->nr >= 0 && READ_ONCE(sys_call_table_ptr[h->nr]) != h->wrapper) {
		pr_warn("susfs_fp: nr %d is not ours any more, leaving it alone\n", h->nr);
		h->installed = false;
		return;
	}

	if (cnr >= 0 && compat_sys_call_table_ptr && h->orig_compat &&
	    READ_ONCE(compat_sys_call_table_ptr[cnr]) == h->wrapper) {
		err = ksu_patch_text(&compat_sys_call_table_ptr[cnr], &h->orig_compat,
				     sizeof(h->orig_compat), KSU_PATCH_TEXT_FLUSH_DCACHE);
		if (err)
			pr_warn("susfs_fp: %s: compat entry %d restore failed %d\n",
				h->name, cnr, err);
		else
			pr_info("susfs_fp: %s: compat entry %d restored\n", h->name, cnr);
	}

	/* nr < 0 means there is no native entry: only the compat one above was ours. */
	if (h->nr >= 0) {
		err = ksu_patch_text(&sys_call_table_ptr[h->nr], &h->orig,
				     sizeof(h->orig), KSU_PATCH_TEXT_FLUSH_DCACHE);
		if (err)
			pr_warn("susfs_fp: nr %d restore failed %d\n", h->nr, err);
		else
			pr_info("susfs_fp: nr %d (%s) disarmed, entry %px restored\n",
				h->nr, h->name, (void *)h->orig);
	}

	h->installed = false;
}

/* Wait until nothing can be inside a wrapper before the module text goes away.
 *
 * Restoring the table entry only stops NEW calls: a task that already picked the
 * wrapper out of the table is running module code right now, and rmmod would free
 * it under its feet.  synchronize_rcu() alone does not cover it - the syscall
 * path is not an RCU read-side section for this purpose - so the same
 * synchronize_rcu_tasks() drain the LSM layer uses is repeated here (resolved at
 * runtime and called through a __nocfi wrapper; a delay is the fallback). */
static void (*fp_sync_rcu_tasks_fn)(void);
static bool fp_sync_looked_up;

static __nocfi void fp_call_drain(void (*fn)(void))
{
	fn();
}

void susfs_fp_drain(void)
{
	if (!fp_sync_looked_up) {
		fp_sync_rcu_tasks_fn = (void *)find_kernel_symbol_exact("synchronize_rcu_tasks");
		fp_sync_looked_up = true;
		if (!fp_sync_rcu_tasks_fn)
			pr_warn("susfs_fp: synchronize_rcu_tasks not found, using a delay\n");
	}

	synchronize_rcu();
	if (fp_sync_rcu_tasks_fn)
		fp_call_drain(fp_sync_rcu_tasks_fn);
	else
		msleep(50);
}
