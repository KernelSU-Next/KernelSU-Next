// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef LKMHOOK_HK_LSM_H
#define LKMHOOK_HK_LSM_H

#include <linux/lsm_hooks.h>
#include <linux/types.h>

struct hk_lsm_layout {
	unsigned long (*resolve)(const char *name);
	int (*type_by_name)(const char *name, u32 *id);
	int (*member_off)(u32 id, const char *member, u32 *bit_off,
			  u32 *bit_sz);
	u32 (*type_size)(u32 id);
};

struct hk_lsm_hook {
	const char *head_name;
	const char *target_name;
	size_t head_offset;
	size_t hook_offset;
	void *replacement;
	void *original;
	struct security_hook_list *entry;
	/* set when the hook went in through a static call, NULL for the list route */
	void *scall;
	bool inserted;	/* set when the entry is ours, insert route */
	/*
	 * the node this library inserts when the list route is taken. it is this
	 * library's own type because the kernel's changed shape in 6.12, and what the
	 * list carries at its second word is the hook either way
	 */
	struct hk_lsm_node {
		struct hlist_node node;
		void *hook;
	} ours;
	int offset;
};

/*
 * the head a hook belongs to is found at install time: by name when the consumer
 * offers a layout, and by the target it carries when it does not. nothing here
 * needs the shape of the object, so the catalogue entry only names the member
 */
#define HK_LSM_HOOK_INIT(member, target_symbol, replacement_fn, off)          \
	{                                                                      \
		.head_name = #member,                                          \
		.target_name = target_symbol,                                  \
		.replacement = (void *)(replacement_fn),                       \
		.offset = (off),                                               \
	}

int hk_lsm_init(const struct hk_lsm_layout *layout);
void hk_lsm_exit(void);
int hk_lsm_hook(struct hk_lsm_hook *hook);
/*
 * the same hook inserted instead of replaced: an own entry takes a free static
 * call slot, every hook the kernel already had keeps running, and the original
 * pointer is not touched. only a kernel with the static call table takes one, the
 * list route has no rcu read section around its walk so a link could not be undone
 */
int hk_lsm_insert(struct hk_lsm_hook *hook);
void hk_lsm_unhook(struct hk_lsm_hook *hook);
int hk_lsm_register(struct hk_lsm_hook *hook);
void hk_lsm_unregister(struct hk_lsm_hook *hook);

#endif
