#ifndef __KSU_H_SELINUX_HIDE
#define __KSU_H_SELINUX_HIDE

#include "uapi/selinux.h"
#include <linux/slab.h>

void ksu_selinux_hide_init();
void ksu_selinux_hide_exit();

// forward declaration — defined in selinux/rules.c which #includes this file's .c
int sepol_expected_argc(u32 cmd);

// its all push, no pop, so we can realloc forever

// types
// :type1:\0:type2:\0:type3:\0
static char *ksu_hide_type_list __read_mostly = NULL;
static size_t ksu_hide_type_len = 0;

// rules
// :src1:\0:tgt1:\0:src2:\0:tgt2:\0:src3:\0:tgt3:\0
static char *ksu_hide_rule_list __read_mostly = NULL;
static size_t ksu_hide_rule_len = 0;

static DEFINE_MUTEX(selinux_hide_list_mutex);

static void ksu_add_probe_to_list(u32 cmd, const char *args[])
{
	if (!args || !args[0])
		return;

	mutex_lock(&selinux_hide_list_mutex);

	int argc = sepol_expected_argc(cmd);

	if (cmd == KSU_SEPOLICY_CMD_TYPE || cmd == KSU_SEPOLICY_CMD_TYPE_ATTR || cmd == KSU_SEPOLICY_CMD_TYPE_STATE || cmd == KSU_SEPOLICY_CMD_ATTR) {
		
		const char *name = args[0];
		size_t needed_len = strlen(name) + 3; // :type:\0

		if (!ksu_hide_type_list)
			goto skip_type_dup_check;

		// anti duplicate
		size_t offset = 0;
		while (offset < ksu_hide_type_len) {
			const char *current_type = ksu_hide_type_list + offset;

			char tmp_buf[64];
			snprintf(tmp_buf, sizeof(tmp_buf), ":%s:", name);

			if (!strcmp(current_type, tmp_buf))
				goto out_unlock;

			offset = offset + strlen(current_type) + 1;
		}

	skip_type_dup_check:
		;
		size_t new_total_len = ksu_hide_type_len + needed_len;

		char *new_ptr = krealloc(ksu_hide_type_list, new_total_len, GFP_KERNEL);
		if (!new_ptr)
			goto out_unlock;

		ksu_hide_type_list = new_ptr;

		char *w_ptr = ksu_hide_type_list + ksu_hide_type_len;
		sprintf(w_ptr, ":%s:", name);

		ksu_hide_type_len = new_total_len;

		pr_info("selinux_hide: tracking type: %s\n", w_ptr );


	} else if (argc >= 2) {

		if (!args[1])
			goto out_unlock;

		const char *src = args[0];
		const char *tgt = args[1];

		size_t src_needed = strlen(src) + 3; // :src:\0
		size_t tgt_needed = strlen(tgt) + 3; // :tgt:\0
		size_t needed_len = src_needed + tgt_needed;

		if (!ksu_hide_rule_list)
			goto skip_rule_dup_check;

		// anti duplicate
		size_t offset = 0;
		while (offset < ksu_hide_rule_len) {
			const char *src_chk = ksu_hide_rule_list + offset;
			size_t src_sz = strlen(src_chk) + 1; // for \0			

			const char *tgt_chk = src_chk + src_sz;
			size_t tgt_sz = strlen(tgt_chk) + 1; // for \0

			char src_buf[64], tgt_buf[64];
			snprintf(src_buf, sizeof(src_buf), ":%s:", src);
			snprintf(tgt_buf, sizeof(tgt_buf), ":%s:", tgt);

			if (!strcmp(src_chk, src_buf) && !strcmp(tgt_chk, tgt_buf))
				goto out_unlock;

			offset = offset + src_sz + tgt_sz;
		}

	skip_rule_dup_check:
		;
		size_t new_total_len = ksu_hide_rule_len + needed_len;
		char *new_ptr = krealloc(ksu_hide_rule_list, new_total_len, GFP_KERNEL);
		if (!new_ptr)
			goto out_unlock;

		ksu_hide_rule_list = new_ptr;

		char *w_ptr_src = ksu_hide_rule_list + ksu_hide_rule_len;
		sprintf(w_ptr_src, ":%s:", src);

		char *w_ptr_tgt = w_ptr_src + strlen(w_ptr_src) + 1; 
		sprintf(w_ptr_tgt, ":%s:", tgt);

		ksu_hide_rule_len = new_total_len;

		pr_info("selinux_hide: tracking rule: %s %s\n", w_ptr_src, w_ptr_tgt);

	}

out_unlock:
	mutex_unlock(&selinux_hide_list_mutex);
}

#if 0
// /selinux/rules.c, linked list
LIST_HEAD(ksu_hide_type_list);
LIST_HEAD(ksu_hide_rule_list);

DECLARE_RWSEM(ksu_sepolicy_list_lock);

struct ksu_type_node {
	struct list_head list;
	char *padded_name;
};

struct ksu_rule_node {
	struct list_head list;
	char *src;
	char *tgt;
};

int sepol_expected_argc(u32 cmd);

static void ksu_add_probe_to_list(u32 cmd, const char *args[])
{
	if (!args || !args[0])
		return;

	int argc = sepol_expected_argc(cmd);
	down_write(&ksu_sepolicy_probelist_lock);

	size_t len;

	if (cmd == KSU_SEPOLICY_CMD_TYPE || cmd == KSU_SEPOLICY_CMD_TYPE_ATTR || cmd == KSU_SEPOLICY_CMD_TYPE_STATE || cmd == KSU_SEPOLICY_CMD_ATTR) {
		
		const char *name = args[0];
		len = strlen(name);

		// no need after rule matching, keep as a reminder though
		//if (!strcmp(name, "zygote") || !strcmp(name, "app_zygote"))
			//goto out_unlock;

		struct ksu_type_node *t_node;
		list_for_each_entry(t_node, &ksu_hide_type_list, list) {
			if (strlen(t_node->padded_name) == (len + 2) && !memcmp(t_node->padded_name + 1, name, len))
				goto out_unlock;
		}

		t_node = kmalloc(sizeof(*t_node), GFP_KERNEL);
		if (!t_node)
			goto out_unlock;
		
		t_node->padded_name = kmalloc(len + 3, GFP_KERNEL);
		if (!t_node->padded_name) {
			kfree(t_node);
			goto out_unlock;
		}
		
		snprintf(t_node->padded_name, len + 3, ":%s:", name);
		list_add(&t_node->list, &ksu_hide_type_list);

		if (IS_ENABLED(CONFIG_KSU_DEBUG))
			pr_info("selinux_hide: tracking type: %s \n", t_node->padded_name);

	} else if (argc >= 2) {
		const char *src = args[0];
		const char *tgt = args[1];

		// for zygote, a x x y rules, we grab x y
		// if (!strcmp(src, "zygote") && args[2] && !strcmp(src, tgt))
		//	tgt = args[2];

		struct ksu_rule_node *r_node;
		list_for_each_entry(r_node, &ksu_hide_rule_list, list) {
			if (strstarts(r_node->src + 1, src) && strstarts(r_node->tgt + 1, tgt))
				goto out_unlock;
		}

		r_node = kmalloc(sizeof(*r_node), GFP_KERNEL);
		if (!r_node)
			goto out_unlock;

		r_node->src = kmalloc(strlen(src) + 3, GFP_KERNEL);
		if (!r_node->src) {
			kfree(r_node);
			goto out_unlock;
		}		
		snprintf(r_node->src, strlen(src) + 3, ":%s:", src);

		r_node->tgt = kmalloc(strlen(tgt) + 3, GFP_KERNEL);
		if (!r_node->tgt) {
			kfree(r_node->src);
			kfree(r_node);
			goto out_unlock;
		}
		snprintf(r_node->tgt, strlen(tgt) + 3, ":%s:", tgt);

		list_add(&r_node->list, &ksu_hide_rule_list);

		if (IS_ENABLED(CONFIG_KSU_DEBUG))
			pr_info("selinux_hide: tracking rule: %s %s \n", r_node->src, r_node->tgt);

	}

out_unlock:
	up_write(&ksu_sepolicy_probelist_lock);
}
#endif

#endif