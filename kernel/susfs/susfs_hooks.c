// SPDX-License-Identifier: GPL-2.0
/*
 * susfs_hooks.c - one registration surface and one target table.
 *
 * Feature layers only call susfs_hook_register()/unregister().  This file owns
 * the target's single installation, its backend and the ordered consumer list.
 *
 * First batch: slow_avc_audit.  The same table shape extends to the proc/stat
 * targets; a target that is not implemented here returns -225 rather than
 * pretending to install.
 */
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include "susfs_hooks.h"
#include "susfs_hook_engine.h"
#include "susfs_log.h"
#include "hk.h"
#include "hk_inline.h"

#define CONSUMERS_MAX 8

enum hook_backend {
	BACKEND_NONE = 0,
	BACKEND_INLINE,
	BACKEND_KPROBE,
};

struct hook_target {
	const char *symbol;
	enum susfs_hook_slot slot;
	const char *wrapper;
	enum hook_backend backend;
	bool installed;
	struct susfs_hook *consumers[CONSUMERS_MAX];
	unsigned int nconsumers;
	struct hk_inline inline_hook;
	struct kprobe kp;
};

static struct hook_target targets[SUSFS_HOOK_TARGET_COUNT];
static DEFINE_MUTEX(susfs_hooks_lock);

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

static int avc_kprobe_pre(struct kprobe *p, struct pt_regs *regs)
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

static int avc_kprobe_pre(struct kprobe *p, struct pt_regs *regs)
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

static int target_is_avc(enum susfs_hook_target target)
{
	return target == SUSFS_HOOK_TARGET_AVC_AUDIT;
}

static struct hook_target *target_of(enum susfs_hook_target target)
{
	if ((unsigned int)target >= SUSFS_HOOK_TARGET_COUNT ||
	    !target_is_avc(target))
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

static int install_inline(struct hook_target *t)
{
	int rc;

	if (!susfs_hook_engine_active() || !susfs_hook_slot_enabled(t->slot))
		return SUSFS_HOOK_INSTALL_FAILED;
	if (t->slot == SUSFS_HOOK_SLOW_AVC_AUDIT)
		susfs_hook_override(t->wrapper, (unsigned long)avc_wrapper);
	rc = hk_inline_hook(&t->inline_hook, t->symbol, t->wrapper);
	if (rc)
		return SUSFS_HOOK_INSTALL_FAILED;
	if (t->slot == SUSFS_HOOK_SLOW_AVC_AUDIT)
		avc_orig = (avc_fn_t)t->inline_hook.orig;
	t->backend = BACKEND_INLINE;
	t->installed = true;
	return SUSFS_HOOK_OK;
}

static int install_kprobe(struct hook_target *t)
{
	t->kp.symbol_name = t->symbol;
	t->kp.pre_handler = t->slot == SUSFS_HOOK_SLOW_AVC_AUDIT ? avc_kprobe_pre : NULL;
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
	else if (t->backend == BACKEND_KPROBE)
		unregister_kprobe(&t->kp);
	t->backend = BACKEND_NONE;
	t->installed = false;
}

static int first_install(struct hook_target *t, enum susfs_hook_backend requested,
			 bool allow_fallback)
{
	int rc;

	if (requested == SUSFS_HOOK_BACKEND_INLINE) {
		rc = install_inline(t);
		if (rc == SUSFS_HOOK_OK)
			return rc;
		if (!allow_fallback)
			return SUSFS_HOOK_INSTALL_FAILED;
		rc = install_kprobe(t);
		return rc == SUSFS_HOOK_OK ? SUSFS_HOOK_FALLBACK : SUSFS_HOOK_INSTALL_FAILED;
	}
	if (requested == SUSFS_HOOK_BACKEND_KPROBE) {
		rc = install_kprobe(t);
		return rc == SUSFS_HOOK_OK ? SUSFS_HOOK_OK : SUSFS_HOOK_INSTALL_FAILED;
	}
	return SUSFS_HOOK_INSTALL_FAILED;
}

int susfs_hook_register(struct susfs_hook *hook)
{
	struct hook_target *t;
	int rc;

	if (!hook || (unsigned int)hook->target >= SUSFS_HOOK_TARGET_COUNT)
		return SUSFS_HOOK_INSTALL_FAILED;
	t = target_of(hook->target);
	if (!t)
		return SUSFS_HOOK_INSTALL_FAILED;
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
	rc = first_install(t, SUSFS_HOOK_BACKEND_INLINE, true);
	if (rc == SUSFS_HOOK_INSTALL_FAILED)
		remove_consumer(t, hook);
	mutex_unlock(&susfs_hooks_lock);
	return rc;
}

int susfs_hook_register_assign(struct susfs_hook *hook,
			       enum susfs_hook_backend backend)
{
	struct hook_target *t;
	int rc;

	if (!hook || (unsigned int)hook->target >= SUSFS_HOOK_TARGET_COUNT ||
	    backend == SUSFS_HOOK_BACKEND_AUTO)
		return SUSFS_HOOK_INSTALL_FAILED;
	t = target_of(hook->target);
	if (!t)
		return SUSFS_HOOK_INSTALL_FAILED;
	mutex_lock(&susfs_hooks_lock);
	if (t->installed) {
		int same = (backend == SUSFS_HOOK_BACKEND_INLINE && t->backend == BACKEND_INLINE) ||
			   (backend == SUSFS_HOOK_BACKEND_KPROBE && t->backend == BACKEND_KPROBE);
		rc = same ? add_consumer(t, hook) : SUSFS_HOOK_INSTALL_FAILED;
		mutex_unlock(&susfs_hooks_lock);
		return rc;
	}
	rc = add_consumer(t, hook);
	if (rc) {
		mutex_unlock(&susfs_hooks_lock);
		return rc;
	}
	rc = first_install(t, backend, false);
	if (rc == SUSFS_HOOK_INSTALL_FAILED)
		remove_consumer(t, hook);
	mutex_unlock(&susfs_hooks_lock);
	return rc;
}

void susfs_hook_unregister(struct susfs_hook *hook)
{
	struct hook_target *t;

	if (!hook || (unsigned int)hook->target >= SUSFS_HOOK_TARGET_COUNT)
		return;
	t = target_of(hook->target);
	if (!t)
		return;
	mutex_lock(&susfs_hooks_lock);
	remove_consumer(t, hook);
	if (!t->nconsumers)
		uninstall_target(t);
	mutex_unlock(&susfs_hooks_lock);
}

int susfs_hooks_init(void)
{
	memset(targets, 0, sizeof(targets));
	targets[SUSFS_HOOK_TARGET_AVC_AUDIT].symbol = "slow_avc_audit";
	targets[SUSFS_HOOK_TARGET_AVC_AUDIT].slot = SUSFS_HOOK_SLOW_AVC_AUDIT;
	targets[SUSFS_HOOK_TARGET_AVC_AUDIT].wrapper = "susfs_avc_wrapper";
	return 0;
}

void susfs_hooks_exit(void)
{
}
