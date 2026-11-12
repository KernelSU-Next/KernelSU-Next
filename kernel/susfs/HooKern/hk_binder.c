// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <uapi/linux/android/binder.h>

#include "hk.h"
#include "hk_binder.h"
#include "hk_cfi.h"
#include "hk_inline.h"

long __nocfi hk_binder_ioctl_wrap(struct file *filp, unsigned int cmd,
				  unsigned long arg);
unsigned long __nocfi hk_binder_copy_wrap(void *alloc, void *buffer,
					  unsigned long buffer_offset,
					  const void __user *from,
					  size_t bytes);
void __nocfi hk_binder_transaction_wrap(void *proc, void *thread,
					struct binder_transaction_data *tr,
					int reply,
					unsigned long extra_buffers_size);

typedef long (*binder_ioctl_fn)(struct file *filp, unsigned int cmd,
				unsigned long arg);
typedef unsigned long (*binder_copy_fn)(void *alloc, void *buffer,
					unsigned long buffer_offset,
					const void __user *from, size_t bytes);
typedef void (*binder_transaction_fn)(void *proc, void *thread,
				      struct binder_transaction_data *tr,
				      int reply,
				      unsigned long extra_buffers_size);

struct hk_binder_state {
	const struct hk_binder_callbacks *cb;
	struct hk_inline ioctl;
	struct hk_inline copy;
	struct hk_inline transaction;
	binder_ioctl_fn orig_ioctl;
	binder_copy_fn orig_copy;
	binder_transaction_fn orig_transaction;
	atomic_t active;
	wait_queue_head_t idle_wq;
	bool draining;
	bool inited;
};

static struct hk_binder_state g_binder;

static void hk_binder_enter(void)
{
	atomic_inc(&g_binder.active);
}

static void hk_binder_leave(void)
{
	if (atomic_dec_and_test(&g_binder.active))
		wake_up_all(&g_binder.idle_wq);
}

static void hk_binder_wait_idle(void)
{
	while (atomic_read(&g_binder.active) > 0)
		wait_event_timeout(g_binder.idle_wq,
				   atomic_read(&g_binder.active) == 0,
				   msecs_to_jiffies(1000));
}

typedef void (*hk_synchronize_rcu_tasks_fn)(void);

static __nocfi void hk_binder_synchronize_rcu_tasks(void)
{
	hk_synchronize_rcu_tasks_fn fn;

	fn = (hk_synchronize_rcu_tasks_fn)hk_resolve("synchronize_rcu_tasks");
	if (fn && hk_ker_addr_ok((unsigned long)fn))
		fn();
}

struct hk_cfi_search {
	const char *name;
	size_t len;
	unsigned long addr;
};

long __nocfi hk_binder_ioctl_wrap(struct file *filp, unsigned int cmd,
				  unsigned long arg)
{
	long ret;

	hk_binder_enter();

	if (!READ_ONCE(g_binder.draining) && g_binder.cb &&
	    g_binder.cb->ioctl)
		g_binder.cb->ioctl(g_binder.cb->priv, filp, cmd, arg);

	if (g_binder.orig_ioctl)
		ret = g_binder.orig_ioctl(filp, cmd, arg);
	else
		ret = -ENOSYS;

	hk_binder_leave();
	return ret;
}

unsigned long __nocfi hk_binder_copy_wrap(void *alloc, void *buffer,
					  unsigned long buffer_offset,
					  const void __user *from,
					  size_t bytes)
{
	unsigned long ret;

	hk_binder_enter();

	if (!READ_ONCE(g_binder.draining) && g_binder.cb &&
	    g_binder.cb->copy_from_user)
		g_binder.cb->copy_from_user(g_binder.cb->priv, alloc, buffer,
					    buffer_offset, from, bytes);

	if (g_binder.orig_copy)
		ret = g_binder.orig_copy(alloc, buffer, buffer_offset, from,
					 bytes);
	else
		ret = bytes;

	hk_binder_leave();
	return ret;
}

void __nocfi hk_binder_transaction_wrap(void *proc, void *thread,
					struct binder_transaction_data *tr,
					int reply,
					unsigned long extra_buffers_size)
{
	struct hk_binder_transaction_event ev = { 0 };
	size_t len = 0;
	bool trace;

	hk_binder_enter();

	trace = !READ_ONCE(g_binder.draining) && g_binder.cb &&
		g_binder.cb->transaction;

	if (trace) {
		if (tr) {
			ev.code = tr->code;
			ev.flags = tr->flags;
			ev.data_size = tr->data_size;
			ev.offsets_size = tr->offsets_size;
			ev.target_handle = tr->target.handle;
			ev.sender_pid = tr->sender_pid;
			ev.sender_euid = tr->sender_euid;

			if (tr->data_size && tr->data.ptr.buffer) {
				len = min_t(size_t, tr->data_size,
					    HK_BINDER_MAX_PAYLOAD);
				if (copy_from_user(ev.payload,
						   (const void __user *)(uintptr_t)tr->data.ptr.buffer,
						   len)) {
					memset(ev.payload, 0,
					       sizeof(ev.payload));
					len = 0;
					ev.payload_truncated = true;
				} else {
					ev.payload_truncated =
						tr->data_size >
						HK_BINDER_MAX_PAYLOAD;
				}
			}
		}
		ev.payload_len = len;
		g_binder.cb->transaction(g_binder.cb->priv, &ev);
	}

	if (g_binder.orig_transaction)
		g_binder.orig_transaction(proc, thread, tr, reply,
					  extra_buffers_size);

	hk_binder_leave();
}

static int hk_binder_install(struct hk_inline *h, const char *sym,
			     const char *wrap, unsigned long *orig)
{
	unsigned long addr;
	int ret;

	addr = hk_resolve(sym);
	if (!addr)
		return -ENOENT;

	ret = hk_inline_hook(h, sym, wrap);
	if (ret)
		return ret;

	*orig = h->orig;
	return 0;
}

int hk_binder_init(const struct hk_binder_callbacks *cb)
{
	int ret;

	if (!cb)
		return -EINVAL;

	memset(&g_binder, 0, sizeof(g_binder));
	g_binder.cb = cb;
	atomic_set(&g_binder.active, 0);
	init_waitqueue_head(&g_binder.idle_wq);
	hk_cfi_bypass();

	ret = hk_binder_install(&g_binder.ioctl, "binder_ioctl",
				"hk_binder_ioctl_wrap",
				(unsigned long *)&g_binder.orig_ioctl);
	if (ret)
		goto err;

	ret = hk_binder_install(&g_binder.copy,
				"binder_alloc_copy_user_to_buffer",
				"hk_binder_copy_wrap",
				(unsigned long *)&g_binder.orig_copy);
	if (ret)
		goto err_ioctl;

	ret = hk_binder_install(&g_binder.transaction, "binder_transaction",
				"hk_binder_transaction_wrap",
				(unsigned long *)&g_binder.orig_transaction);
	if (ret == -ENOENT) {
		ret = 0;
	} else if (ret) {
		goto err_copy;
	}

	g_binder.inited = true;
	pr_info("[lkmhook] binder trace init\n");
	return 0;

err_copy:
	hk_inline_unhook(&g_binder.copy);
err_ioctl:
	hk_inline_unhook(&g_binder.ioctl);
err:
	memset(&g_binder, 0, sizeof(g_binder));
	pr_warn("[lkmhook] binder trace init failed %d\n", ret);
	return ret;
}

void hk_binder_exit(void)
{
	if (!g_binder.inited)
		return;

	WRITE_ONCE(g_binder.draining, true);
	hk_inline_disable(&g_binder.transaction);
	hk_inline_disable(&g_binder.copy);
	hk_inline_disable(&g_binder.ioctl);
	hk_binder_synchronize_rcu_tasks();
	hk_binder_wait_idle();
	hk_inline_free(&g_binder.transaction);
	hk_inline_free(&g_binder.copy);
	hk_inline_free(&g_binder.ioctl);

	memset(&g_binder, 0, sizeof(g_binder));
	pr_info("[lkmhook] binder trace exit\n");
}
