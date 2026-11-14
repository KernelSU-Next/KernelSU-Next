// SPDX-License-Identifier: GPL-2.0
/*
 * susfs_hooks.c - one registration surface and one target table.
 *
 * Feature layers only call susfs_hook_register()/unregister().  This file owns
 * the installation, the backend choice and the ordered consumer list.
 *
 * Two consumer shapes share the table:
 *
 *  - structured: the target has a centre-owned inline wrapper (only these can
 *    be inline-hooked), the feature supplies pre()/post() over a typed context
 *    and the centre keeps one installation plus a consumer list;
 *  - raw: the feature owns the kprobe/kretprobe/kretprobe entry+ret handlers
 *    (and the instance data layout), the centre only registers and removes the
 *    probe set.  This is the migration shape for targets whose handler has to
 *    reinterpret pt_regs or return values itself; it moves installation into
 *    this layer without pretending the target has a portable context.
 *
 * sys_call_table replacement (FP) is the third backend and is owned here too:
 * the feature hands over its struct susfs_fp_hook.
 */
#include <linux/kernel.h>
#include <linux/bits.h>
#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/kprobes.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include "susfs_hooks.h"
#include "susfs_hook_engine.h"
#include "susfs_fp_hook.h"
#include "susfs_kprobe.h"
#include "susfs_log.h"
#include "patch_memory.h"
#include "lsm_hook.h"
#include "hk.h"
#include "hk_inline.h"

#define CONSUMERS_MAX 8
#define RAW_PROBES_MAX 64

enum hook_backend {
	BACKEND_NONE = 0,
	BACKEND_INLINE,
	BACKEND_KPROBE,
	BACKEND_FP,
	BACKEND_TABLE,
};

struct hook_target {
	const char *symbol;
	int slot;				/* -1: not in the inline map */
	const char *wrapper;			/* NULL: no structured wrapper */
	enum hook_backend backend;		/* structured installation */
	bool installed;
	struct susfs_hook *consumers[CONSUMERS_MAX];
	unsigned int nconsumers;
	struct hk_inline inline_hook;
	struct kprobe kp;
	struct susfs_hook *hook;		/* first consumer, for raw fallback */
};

static struct hook_target targets[SUSFS_HOOK_TARGET_COUNT];
static DEFINE_MUTEX(susfs_hooks_lock);

/* ---- structured target: slow_avc_audit ---- */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
typedef void (*avc_fn_t)(u32 ssid, u32 tsid, u16 tclass, u32 requested,
			 u32 audited, u32 denied, int result, void *a);
static avc_fn_t avc_orig;

static void __visible __nocfi noinline
avc_wrapper(u32 ssid, u32 tsid, u16 tclass, u32 requested, u32 audited,
	    u32 denied, int result, void *a)
{
	struct susfs_avc_ctx ctx = {
		.ssid = ssid, .tsid = tsid, .tclass = tclass,
		.requested = requested, .audited = audited, .denied = denied,
		.result = result, .a = a,
	};
	unsigned int i;
	int skip = 0;

	for (i = 0; i < CONSUMERS_MAX; i++) {
		struct susfs_hook *c = READ_ONCE(targets[SUSFS_HOOK_TARGET_AVC_AUDIT].consumers[i]);

		if (c && c->pre)
			skip |= c->pre(&ctx);
	}
	if (skip)
		return;
	if (avc_orig)
		avc_orig(ctx.ssid, ctx.tsid, ctx.tclass, ctx.requested,
			 ctx.audited, ctx.denied, ctx.result, ctx.a);
}

static int __nocfi avc_kprobe_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct susfs_avc_ctx ctx = {
		.ssid = (u32)regs->regs[0], .tsid = (u32)regs->regs[1],
		.tclass = (u16)regs->regs[2], .requested = (u32)regs->regs[3],
		.audited = (u32)regs->regs[4], .denied = (u32)regs->regs[5],
		.result = (int)regs->regs[6], .a = (void *)regs->regs[7],
	};
	unsigned int i;

	for (i = 0; i < CONSUMERS_MAX; i++) {
		struct susfs_hook *c = READ_ONCE(targets[SUSFS_HOOK_TARGET_AVC_AUDIT].consumers[i]);

		if (c && c->pre && c->pre(&ctx))
			return 1;
	}
	regs->regs[0] = ctx.ssid;
	regs->regs[1] = ctx.tsid;
	return 0;
}
#else
typedef void (*avc_fn_t)(void *state, u32 ssid, u32 tsid, u16 tclass,
			 u32 requested, u32 audited, u32 denied, int result,
			 void *a);
static avc_fn_t avc_orig;

static void __visible __nocfi noinline
avc_wrapper(void *state, u32 ssid, u32 tsid, u16 tclass, u32 requested,
	    u32 audited, u32 denied, int result, void *a)
{
	struct susfs_avc_ctx ctx = {
		.state = state, .ssid = ssid, .tsid = tsid, .tclass = tclass,
		.requested = requested, .audited = audited, .denied = denied,
		.result = result, .a = a,
	};
	unsigned int i;
	int skip = 0;

	for (i = 0; i < CONSUMERS_MAX; i++) {
		struct susfs_hook *c = READ_ONCE(targets[SUSFS_HOOK_TARGET_AVC_AUDIT].consumers[i]);

		if (c && c->pre)
			skip |= c->pre(&ctx);
	}
	if (skip)
		return;
	if (avc_orig)
		avc_orig(ctx.state, ctx.ssid, ctx.tsid, ctx.tclass, ctx.requested,
			 ctx.audited, ctx.denied, ctx.result, ctx.a);
}

static int __nocfi avc_kprobe_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct susfs_avc_ctx ctx = {
		.state = (void *)regs->regs[0], .ssid = (u32)regs->regs[1],
		.tsid = (u32)regs->regs[2], .tclass = (u16)regs->regs[3],
		.requested = (u32)regs->regs[4], .audited = (u32)regs->regs[5],
		.denied = (u32)regs->regs[6], .result = (int)regs->regs[7],
		.a = (void *)regs->regs[8],
	};
	unsigned int i;

	for (i = 0; i < CONSUMERS_MAX; i++) {
		struct susfs_hook *c = READ_ONCE(targets[SUSFS_HOOK_TARGET_AVC_AUDIT].consumers[i]);

		if (c && c->pre && c->pre(&ctx))
			return 1;
	}
	regs->regs[1] = ctx.ssid;
	regs->regs[2] = ctx.tsid;
	return 0;
}
#endif

static bool target_is_avc(enum susfs_hook_target target)
{
	return target == SUSFS_HOOK_TARGET_AVC_AUDIT;
}

/* ---- structured target: VFS inode_permission (sus_path) ---- */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
typedef int (*inode_perm_fn_t)(void *idmap, struct inode *inode, int mask);
static inode_perm_fn_t inode_perm_orig;

static int __visible __nocfi noinline
inode_perm_wrapper(void *idmap, struct inode *inode, int mask)
{
	struct susfs_inode_ctx ctx = { .inode = inode, .mask = mask, .ret = 0 };
	unsigned int i;
	int skip = 0;

	for (i = 0; i < CONSUMERS_MAX; i++) {
		struct susfs_hook *c = READ_ONCE(targets[SUSFS_HOOK_TARGET_INODE_PERMISSION].consumers[i]);

		if (c && c->pre)
			skip |= c->pre(&ctx);
	}
	if (skip)
		return (int)ctx.ret;
	return inode_perm_orig ? inode_perm_orig(idmap, inode, mask) : 0;
}
#else
typedef int (*inode_perm_fn_t)(struct inode *inode, int mask);
static inode_perm_fn_t inode_perm_orig;

static int __visible __nocfi noinline
inode_perm_wrapper(struct inode *inode, int mask)
{
	struct susfs_inode_ctx ctx = { .inode = inode, .mask = mask, .ret = 0 };
	unsigned int i;
	int skip = 0;

	for (i = 0; i < CONSUMERS_MAX; i++) {
		struct susfs_hook *c = READ_ONCE(targets[SUSFS_HOOK_TARGET_INODE_PERMISSION].consumers[i]);

		if (c && c->pre)
			skip |= c->pre(&ctx);
	}
	if (skip)
		return (int)ctx.ret;
	return inode_perm_orig ? inode_perm_orig(inode, mask) : 0;
}
#endif

/* ---- structured target: VFS vfs_open (sus_path), entry-stub form ----
 *
 * vfs_open starts with a PAC prologue on this tree, so a C wrapper cannot be
 * its detour (the trampoline would sign the wrapper's frame).  The stub runs
 * with the target's SP and LR, restores them, and jumps to the trampoline,
 * which replays the paciasp exactly as the real entry would.  The stub is
 * assembly (susfs_vfs_stub.S) and calls this decision function.
 */
extern void susfs_vfs_open_stub(void);
extern unsigned long susfs_vfs_open_tramp;

int __nocfi susfs_vfs_open_decide(const struct path *path, struct file *file)
{
	struct susfs_inode_ctx ctx = {
		.inode = (path && path->dentry) ? d_inode(path->dentry) : NULL,
		.ret = 0,
	};
	unsigned int i;
	int skip = 0;

	for (i = 0; i < CONSUMERS_MAX; i++) {
		struct susfs_hook *c = READ_ONCE(targets[SUSFS_HOOK_TARGET_VFS_OPEN].consumers[i]);

		if (c && c->pre)
			skip |= c->pre(&ctx);
	}
	return skip ? (int)ctx.ret : 0;
}

/* Only a target with a centre-owned wrapper can be inline-hooked and can carry
 * more than one consumer.  Raw targets are installed per consumer. */
static struct hook_target *target_of(enum susfs_hook_target target)
{
	if ((unsigned int)target >= SUSFS_HOOK_TARGET_COUNT)
		return NULL;
	if (!target_is_avc(target) &&
	    target != SUSFS_HOOK_TARGET_INODE_PERMISSION &&
	    target != SUSFS_HOOK_TARGET_VFS_OPEN &&
	    target != SUSFS_HOOK_TARGET_ARM64_SYS_NEWUNAME &&
	    target != SUSFS_HOOK_TARGET_S_SHOW &&
	    target != SUSFS_HOOK_TARGET_M_SHOW &&
	    target != SUSFS_HOOK_TARGET_SHOW_MAP &&
	    target != SUSFS_HOOK_TARGET_SHOW_SMAP)
		return NULL;
	return &targets[target];
}

static bool consumer_present(struct hook_target *t, struct susfs_hook *hook)
{
	unsigned int i;

	for (i = 0; i < CONSUMERS_MAX; i++)
		if (t->consumers[i] == hook)
			return true;
	return false;
}

static int add_consumer(struct hook_target *t, struct susfs_hook *hook)
{
	unsigned int i;

	if (consumer_present(t, hook))
		return SUSFS_HOOK_OK;
	for (i = 0; i < CONSUMERS_MAX; i++) {
		if (!t->consumers[i]) {
			WRITE_ONCE(t->consumers[i], hook);
			t->nconsumers++;
			return SUSFS_HOOK_OK;
		}
	}
	return SUSFS_HOOK_INSTALL_FAILED;
}

static void remove_consumer(struct hook_target *t, struct susfs_hook *hook)
{
	unsigned int i;

	for (i = 0; i < CONSUMERS_MAX; i++) {
		if (t->consumers[i] == hook) {
			WRITE_ONCE(t->consumers[i], NULL);
			t->nconsumers--;
			return;
		}
	}
}

/* The uname layer keeps its wrapper and orig pointer here so this file does not
 * need the feature's internals: install_inline() hands the trampoline over. */
extern long susfs_uname_wrap(const struct pt_regs *regs);
extern long (*susfs_uname_orig)(const struct pt_regs *regs);

static int install_inline(struct hook_target *t, struct susfs_hook *h)
{
	int rc;

	t->hook = h;
	/* The table-identity callbacks have no map slot: their symbol is resolved
	 * by the feature and installed through the PAC-safe stub form. */
	if (h->target == SUSFS_HOOK_TARGET_S_SHOW ||
	    h->target == SUSFS_HOOK_TARGET_M_SHOW) {
		if (!h->stub || !h->tramp)
			return SUSFS_HOOK_INSTALL_FAILED;
		rc = hk_inline_hook_stub(&t->inline_hook, t->symbol,
					 (unsigned long)h->stub, h->tramp);
		if (rc)
			return SUSFS_HOOK_INSTALL_FAILED;
		t->backend = BACKEND_INLINE;
		t->installed = true;
		return SUSFS_HOOK_OK;
	}
	if (!susfs_hook_engine_active() || t->slot < 0 ||
	    !susfs_hook_slot_enabled((enum susfs_hook_slot)t->slot))
		return SUSFS_HOOK_INSTALL_FAILED;
	if (t->slot == SUSFS_HOOK_VFS_OPEN) {
		rc = hk_inline_hook_stub(&t->inline_hook, t->symbol,
					 (unsigned long)susfs_vfs_open_stub,
					 &susfs_vfs_open_tramp);
		if (rc)
			return SUSFS_HOOK_INSTALL_FAILED;
		t->backend = BACKEND_INLINE;
		t->installed = true;
		return SUSFS_HOOK_OK;
	}
	if (t->slot == SUSFS_HOOK_SLOW_AVC_AUDIT)
		susfs_hook_override(t->wrapper, (unsigned long)avc_wrapper);
	else if (t->slot == SUSFS_HOOK_INODE_PERMISSION)
		susfs_hook_override(t->wrapper, (unsigned long)inode_perm_wrapper);
	else if (t->slot == SUSFS_HOOK_ARM64_SYS_NEWUNAME)
		susfs_hook_override(t->wrapper, (unsigned long)susfs_uname_wrap);
	rc = hk_inline_hook(&t->inline_hook, t->symbol, t->wrapper);
	if (rc)
		return SUSFS_HOOK_INSTALL_FAILED;
	if (t->slot == SUSFS_HOOK_SLOW_AVC_AUDIT)
		avc_orig = (avc_fn_t)t->inline_hook.orig;
	else if (t->slot == SUSFS_HOOK_INODE_PERMISSION)
		inode_perm_orig = (inode_perm_fn_t)t->inline_hook.orig;
	else if (t->slot == SUSFS_HOOK_ARM64_SYS_NEWUNAME)
		susfs_uname_orig = (long (*)(const struct pt_regs *))t->inline_hook.orig;
	t->backend = BACKEND_INLINE;
	t->installed = true;
	return SUSFS_HOOK_OK;
}

static int __nocfi vfs_open_kprobe_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct path *path = (struct path *)regs->regs[0];
	struct susfs_inode_ctx ctx = {
		.inode = (path && path->dentry) ? d_inode(path->dentry) : NULL,
		.ret = 0,
	};
	unsigned int i;
	int skip = 0;

	for (i = 0; i < CONSUMERS_MAX; i++) {
		struct susfs_hook *c = READ_ONCE(targets[SUSFS_HOOK_TARGET_VFS_OPEN].consumers[i]);

		if (c && c->pre)
			skip |= c->pre(&ctx);
	}
	if (!skip)
		return 0;
	/* Answer as vfs_open would have: return to the caller with ctx.ret. */
	regs->pc = regs->regs[30];
	regs->regs[0] = (unsigned long)ctx.ret;
	return 1;
}

static int raw_probes_install(struct susfs_hook *h);
static void raw_probes_uninstall(struct susfs_hook *h);

static int install_table(struct hook_target *t, struct susfs_hook *h)
{
	struct seq_operations *op = h->seq_op;

	t->hook = h;
	if (!op || !h->seq_wrap)
		return SUSFS_HOOK_INSTALL_FAILED;
	h->seq_orig = (void *)READ_ONCE(op->show);
	if (!h->seq_orig)
		return SUSFS_HOOK_INSTALL_FAILED;
	if (ksu_patch_text((void *)&op->show, &h->seq_wrap,
			   sizeof(h->seq_wrap), KSU_PATCH_TEXT_FLUSH_DCACHE))
		return SUSFS_HOOK_INSTALL_FAILED;
	t->backend = BACKEND_TABLE;
	t->installed = true;
	return SUSFS_HOOK_OK;
}

static int install_kprobe(struct hook_target *t, struct susfs_hook *h)
{
	int rc;

	t->hook = h;
	/* A structured target may still fall back to the feature's own probe set
	 * (uname's kretprobe): the centre installs it, the handler stays there. */
	if (h->probes && h->nprobes) {
		rc = raw_probes_install(h);
		if (rc != SUSFS_HOOK_OK && rc != SUSFS_HOOK_FALLBACK)
			return SUSFS_HOOK_INSTALL_FAILED;
		t->backend = BACKEND_KPROBE;
		t->installed = true;
		return SUSFS_HOOK_OK;
	}
	t->kp.symbol_name = t->symbol;
	if (t->slot == SUSFS_HOOK_SLOW_AVC_AUDIT)
		t->kp.pre_handler = avc_kprobe_pre;
	else if (t->slot == SUSFS_HOOK_VFS_OPEN)
		t->kp.pre_handler = vfs_open_kprobe_pre;
	else
		t->kp.pre_handler = NULL;
	if (!t->kp.pre_handler)
		return SUSFS_HOOK_INSTALL_FAILED;
	if (register_kprobe(&t->kp))
		return SUSFS_HOOK_INSTALL_FAILED;
	t->backend = BACKEND_KPROBE;
	t->installed = true;
	return SUSFS_HOOK_OK;
}

static void uninstall_target(struct hook_target *t)
{
	if (!t->installed)
		return;
	if (t->backend == BACKEND_INLINE)
		hk_inline_unhook(&t->inline_hook);
	else if (t->backend == BACKEND_KPROBE) {
		if (t->hook && t->hook->probes && t->hook->nprobes)
			raw_probes_uninstall(t->hook);
		else
			unregister_kprobe(&t->kp);
	} else if (t->backend == BACKEND_TABLE) {
		struct susfs_hook *h = t->hook;

		if (h && h->seq_op && h->seq_orig &&
		    ksu_patch_text((void *)&h->seq_op->show, &h->seq_orig,
				   sizeof(h->seq_orig),
				   KSU_PATCH_TEXT_FLUSH_DCACHE))
			pr_warn("susfs_hooks: %s table restore failed\n",
				h->name);
	}
	t->backend = BACKEND_NONE;
	t->installed = false;
}

/* ---- raw consumers: centre-owned installation of feature-owned probes ---- */

static bool raw_hook(const struct susfs_hook *h)
{
	return h->fp || h->probes || h->lsm;
}

static void raw_probes_rollback(struct susfs_hook *h, unsigned int upto)
{
	unsigned int i;

	for (i = 0; i < upto; i++) {
		if (!(h->probe_mask & BIT_ULL(i)))
			continue;
		if (h->probes[i].kr) {
			unregister_kretprobe(h->probes[i].kr);
			susfs_krp_forget_addr(h->probes[i].kr);
		} else if (h->probes[i].kp) {
			unregister_kprobe(h->probes[i].kp);
			susfs_kp_forget_addr(h->probes[i].kp);
		}
	}
	h->probe_mask = 0;
}

static int raw_probes_install(struct susfs_hook *h)
{
	unsigned int i;
	int inst = 0, degraded = 0;

	if (!h->probes || !h->nprobes || h->nprobes > RAW_PROBES_MAX)
		return SUSFS_HOOK_INSTALL_FAILED;
	h->probe_mask = 0;
	for (i = 0; i < h->nprobes; i++) {
		const struct susfs_hook_probe *p = &h->probes[i];
		int rc;

		if (p->kr) {
			rc = register_kretprobe(p->kr);
			if (rc) {
				susfs_krp_forget_addr(p->kr);
			} else {
				h->probe_mask |= BIT_ULL(i);
				inst++;
				continue;
			}
		} else if (p->kp) {
			rc = register_kprobe(p->kp);
			if (rc) {
				susfs_kp_forget_addr(p->kp);
			} else {
				h->probe_mask |= BIT_ULL(i);
				inst++;
				continue;
			}
		} else {
			rc = -EINVAL;
		}
		if (p->required) {
			raw_probes_rollback(h, i);
			return SUSFS_HOOK_INSTALL_FAILED;
		}
		degraded = 1;
	}
	if (!inst)
		return SUSFS_HOOK_INSTALL_FAILED;
	h->installed = true;
	h->backend = SUSFS_HOOK_BACKEND_KPROBE;
	return degraded ? SUSFS_HOOK_FALLBACK : SUSFS_HOOK_OK;
}

static void raw_probes_uninstall(struct susfs_hook *h)
{
	raw_probes_rollback(h, h->nprobes);
}

static int raw_fp_install(struct susfs_hook *h)
{
	int rc;

	if (!h->fp)
		return SUSFS_HOOK_INSTALL_FAILED;
	rc = susfs_fp_install(h->fp);
	if (rc)
		return SUSFS_HOOK_INSTALL_FAILED;
	h->installed = true;
	h->backend = SUSFS_HOOK_BACKEND_FP;
	return SUSFS_HOOK_OK;
}

static void raw_fp_uninstall(struct susfs_hook *h)
{
	susfs_fp_remove(h->fp);
	h->installed = false;
	h->backend = SUSFS_HOOK_BACKEND_NONE;
}

static int raw_lsm_install(struct susfs_hook *h)
{
	int rc;

	if (!h->lsm)
		return SUSFS_HOOK_INSTALL_FAILED;
	rc = ksu_register_lsm_hook(h->lsm);
	if (rc)
		return SUSFS_HOOK_INSTALL_FAILED;
	h->installed = true;
	h->backend = SUSFS_HOOK_BACKEND_LSM;
	return SUSFS_HOOK_OK;
}

static void raw_lsm_uninstall(struct susfs_hook *h)
{
	ksu_unregister_lsm_hook(h->lsm);
	h->installed = false;
	h->backend = SUSFS_HOOK_BACKEND_NONE;
}

static int raw_install_auto(struct susfs_hook *h)
{
	int rc;

	/* syscall targets prefer the table replacement: no breakpoint, no
	 * kprobe locking, and the compat/native split is explicit.  A failed FP
	 * install keeps the raw probes usable, exactly the old two-path shape. */
	if (h->fp) {
		rc = raw_fp_install(h);
		if (!rc)
			return SUSFS_HOOK_OK;
		if (!h->probes)
			return SUSFS_HOOK_INSTALL_FAILED;
		rc = raw_probes_install(h);
		return rc == SUSFS_HOOK_OK ? SUSFS_HOOK_FALLBACK : rc;
	}
	if (h->lsm)
		return raw_lsm_install(h);
	return raw_probes_install(h);
}

/* ---- registration ---- */

int susfs_hook_register(struct susfs_hook *hook)
{
	struct hook_target *t;
	int rc;

	if (!hook || (unsigned int)hook->target >= SUSFS_HOOK_TARGET_COUNT)
		return SUSFS_HOOK_INSTALL_FAILED;

	/* Idempotent for an already installed raw hook: do not touch the live
	 * probe set, just report which backend it ended up on. */
	if (hook->backend == SUSFS_HOOK_BACKEND_FP)
		return SUSFS_HOOK_OK;
	if (hook->backend == SUSFS_HOOK_BACKEND_KPROBE)
		return SUSFS_HOOK_FALLBACK;

	hook->installed = false;
	hook->probe_mask = 0;
	hook->backend = SUSFS_HOOK_BACKEND_NONE;

	t = target_of(hook->target);
	if (!t) {
		if (!raw_hook(hook))
			return SUSFS_HOOK_INSTALL_FAILED;
		mutex_lock(&susfs_hooks_lock);
		rc = raw_install_auto(hook);
		mutex_unlock(&susfs_hooks_lock);
		return rc;
	}

	mutex_lock(&susfs_hooks_lock);
	if (t->installed) {
		rc = add_consumer(t, hook);
		if (!rc)
			rc = t->backend == BACKEND_KPROBE ? SUSFS_HOOK_FALLBACK : SUSFS_HOOK_OK;
		mutex_unlock(&susfs_hooks_lock);
		return rc;
	}
	rc = add_consumer(t, hook);
	if (rc) {
		mutex_unlock(&susfs_hooks_lock);
		return rc;
	}
	rc = install_inline(t, hook);
	if (rc == SUSFS_HOOK_OK) {
		mutex_unlock(&susfs_hooks_lock);
		return rc;
	}
	rc = install_kprobe(t, hook);
	mutex_unlock(&susfs_hooks_lock);
	if (rc == SUSFS_HOOK_OK)
		return SUSFS_HOOK_FALLBACK;
	remove_consumer(t, hook);
	return SUSFS_HOOK_INSTALL_FAILED;
}

int susfs_hook_register_assign(struct susfs_hook *hook,
			       enum susfs_hook_backend backend)
{
	struct hook_target *t;
	int rc;

	if (!hook || (unsigned int)hook->target >= SUSFS_HOOK_TARGET_COUNT ||
	    backend == SUSFS_HOOK_BACKEND_AUTO)
		return SUSFS_HOOK_INSTALL_FAILED;

	if (hook->backend != SUSFS_HOOK_BACKEND_NONE)
		return hook->backend == backend ? SUSFS_HOOK_OK
						: SUSFS_HOOK_INSTALL_FAILED;

	hook->installed = false;
	hook->probe_mask = 0;
	hook->backend = SUSFS_HOOK_BACKEND_NONE;

	t = target_of(hook->target);
	if (!t) {
		if (!raw_hook(hook))
			return SUSFS_HOOK_INSTALL_FAILED;
		mutex_lock(&susfs_hooks_lock);
		switch (backend) {
		case SUSFS_HOOK_BACKEND_FP:
			rc = raw_fp_install(hook);
			break;
		case SUSFS_HOOK_BACKEND_KPROBE:
			rc = raw_probes_install(hook);
			if (rc == SUSFS_HOOK_FALLBACK)
				rc = SUSFS_HOOK_OK;
			break;
		case SUSFS_HOOK_BACKEND_LSM:
			rc = raw_lsm_install(hook);
			break;
		default:
			rc = SUSFS_HOOK_INSTALL_FAILED;
			break;
		}
		mutex_unlock(&susfs_hooks_lock);
		return rc == SUSFS_HOOK_OK ? SUSFS_HOOK_OK : SUSFS_HOOK_INSTALL_FAILED;
	}

	mutex_lock(&susfs_hooks_lock);
	if (t->installed) {
		int same = (backend == SUSFS_HOOK_BACKEND_INLINE && t->backend == BACKEND_INLINE) ||
			   (backend == SUSFS_HOOK_BACKEND_KPROBE && t->backend == BACKEND_KPROBE) ||
			   (backend == SUSFS_HOOK_BACKEND_TABLE && t->backend == BACKEND_TABLE);
		rc = same ? add_consumer(t, hook) : SUSFS_HOOK_INSTALL_FAILED;
		mutex_unlock(&susfs_hooks_lock);
		return rc;
	}
	rc = add_consumer(t, hook);
	if (rc) {
		mutex_unlock(&susfs_hooks_lock);
		return rc;
	}
	if (backend == SUSFS_HOOK_BACKEND_INLINE)
		rc = install_inline(t, hook);
	else if (backend == SUSFS_HOOK_BACKEND_KPROBE)
		rc = install_kprobe(t, hook);
	else if (backend == SUSFS_HOOK_BACKEND_TABLE)
		rc = install_table(t, hook);
	else
		rc = SUSFS_HOOK_INSTALL_FAILED;
	mutex_unlock(&susfs_hooks_lock);
	if (rc != SUSFS_HOOK_OK)
		remove_consumer(t, hook);
	return rc == SUSFS_HOOK_OK ? SUSFS_HOOK_OK : SUSFS_HOOK_INSTALL_FAILED;
}

void susfs_hook_unregister(struct susfs_hook *hook)
{
	struct hook_target *t;

	if (!hook || (unsigned int)hook->target >= SUSFS_HOOK_TARGET_COUNT)
		return;
	t = target_of(hook->target);
	mutex_lock(&susfs_hooks_lock);
	if (!t) {
		if (raw_hook(hook)) {
			if (hook->backend == SUSFS_HOOK_BACKEND_FP)
				raw_fp_uninstall(hook);
			else if (hook->backend == SUSFS_HOOK_BACKEND_KPROBE)
				raw_probes_uninstall(hook);
			else if (hook->backend == SUSFS_HOOK_BACKEND_LSM)
				raw_lsm_uninstall(hook);
		}
		mutex_unlock(&susfs_hooks_lock);
		return;
	}
	remove_consumer(t, hook);
	if (!t->nconsumers)
		uninstall_target(t);
	mutex_unlock(&susfs_hooks_lock);
}

/* ---- target table ---- */

struct target_desc {
	const char *symbol;
	int slot;
	const char *wrapper;
};

static const struct target_desc target_desc[SUSFS_HOOK_TARGET_COUNT] = {
	[SUSFS_HOOK_TARGET_AVC_AUDIT]			= { "slow_avc_audit", 2, "susfs_avc_wrapper" },
	[SUSFS_HOOK_TARGET_SHOW_MAP_VMA]		= { "show_map_vma", 4, NULL },
	[SUSFS_HOOK_TARGET_SHOW_SMAP]			= { "show_smap", 5, NULL },
	[SUSFS_HOOK_TARGET_SHOW_MAP]			= { "show_map", 6, NULL },
	[SUSFS_HOOK_TARGET_WALK_PAGE_RANGE]		= { "walk_page_range", 7, NULL },
	[SUSFS_HOOK_TARGET_WALK_PAGE_VMA]		= { "walk_page_vma", 8, NULL },
	[SUSFS_HOOK_TARGET_GET_USER_PAGES_REMOTE]	= { "get_user_pages_remote", 9, NULL },
	[SUSFS_HOOK_TARGET_GET_USER_PAGES_REMOTE_INNER]	= { "__get_user_pages_remote", 10, NULL },
	[SUSFS_HOOK_TARGET_PIN_USER_PAGES_REMOTE]	= { "pin_user_pages_remote", 35, NULL },
	[SUSFS_HOOK_TARGET_MAP_FILES_GET_LINK]		= { "map_files_get_link", 11, NULL },
	[SUSFS_HOOK_TARGET_D_PATH]			= { "d_path", 12, NULL },
	[SUSFS_HOOK_TARGET_VFS_STATFS]			= { "vfs_statfs", 13, NULL },
	[SUSFS_HOOK_TARGET_SHOW_VFSSTAT]		= { "show_vfsstat", 14, NULL },
	[SUSFS_HOOK_TARGET_SHOW_MOUNTINFO]		= { "show_mountinfo", 15, NULL },
	[SUSFS_HOOK_TARGET_SHOW_VFSMNT]			= { "show_vfsmnt", 16, NULL },
	[SUSFS_HOOK_TARGET_ARM64_SYS_STATX]		= { "__arm64_sys_statx", 17, NULL },
	[SUSFS_HOOK_TARGET_DO_STATX]			= { "do_statx", 18, NULL },
	[SUSFS_HOOK_TARGET_GENERIC_SHUTDOWN_SUPER]	= { "generic_shutdown_super", 19, NULL },
	[SUSFS_HOOK_TARGET_COPY_MNT_NS]			= { "copy_mnt_ns", 20, NULL },
	[SUSFS_HOOK_TARGET_ATTACH_RECURSIVE_MNT]	= { "attach_recursive_mnt", 21, NULL },
	[SUSFS_HOOK_TARGET_ARM64_SYS_NEWUNAME]		= { "__arm64_sys_newuname", 3, "susfs_uname_wrap" },
	[SUSFS_HOOK_TARGET_VFS_OPEN]			= { "vfs_open", 0, NULL },
	[SUSFS_HOOK_TARGET_PATH_OPENAT]			= { "path_openat", 1, NULL },
	[SUSFS_HOOK_TARGET_ARM64_SYS_REBOOT]		= { "__arm64_sys_reboot", -1, NULL },
	[SUSFS_HOOK_TARGET_SEQ_SHOW]			= { "seq_show", -1, NULL },
	[SUSFS_HOOK_TARGET_LSM]				= { NULL, -1, NULL },
	[SUSFS_HOOK_TARGET_FP]				= { NULL, -1, NULL },
	[SUSFS_HOOK_TARGET_INODE_PERMISSION]		= { "inode_permission", 24, "susfs_inode_perm_wrapper" },
	[SUSFS_HOOK_TARGET_S_SHOW]			= { "s_show", -1, NULL },
	[SUSFS_HOOK_TARGET_M_SHOW]			= { "m_show", -1, NULL },
};

int susfs_hooks_init(void)
{
	unsigned int i;

	memset(targets, 0, sizeof(targets));
	for (i = 0; i < SUSFS_HOOK_TARGET_COUNT; i++) {
		targets[i].symbol = target_desc[i].symbol;
		targets[i].slot = target_desc[i].slot;
		targets[i].wrapper = target_desc[i].wrapper;
	}
	return 0;
}

void susfs_hooks_exit(void)
{
}
