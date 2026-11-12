// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef LKMHOOK_HK_BINDER_H
#define LKMHOOK_HK_BINDER_H

#include <linux/types.h>

#define HK_BINDER_MAX_PAYLOAD 256

struct hk_binder_transaction_event {
	unsigned int code;
	unsigned int flags;
	unsigned long data_size;
	unsigned long offsets_size;
	unsigned int target_handle;
	unsigned int sender_pid;
	unsigned int sender_euid;
	unsigned char payload[HK_BINDER_MAX_PAYLOAD];
	size_t payload_len;
	bool payload_truncated;
};

struct hk_binder_callbacks {
	void (*ioctl)(void *priv, void *filp, unsigned int cmd,
		      unsigned long arg);
	void (*copy_from_user)(void *priv, void *alloc, void *buffer,
			       unsigned long buffer_offset,
			       const void __user *from, size_t bytes);
	void (*transaction)(void *priv,
			    const struct hk_binder_transaction_event *ev);
	void *priv;
};

int hk_binder_init(const struct hk_binder_callbacks *cb);
void hk_binder_exit(void);

#endif
