/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SUSFS_LSM_HOOK_H
#define __SUSFS_LSM_HOOK_H

#include <linux/lsm_hooks.h>
#include <linux/stddef.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#define KSU_LSM_HOOK_HEADS_TYPE struct lsm_static_calls_table
#else
#define KSU_LSM_HOOK_HEADS_TYPE struct security_hook_heads
#endif

struct ksu_lsm_hook {
    const char *head_name;
    const char *target_name;
    size_t head_offset;
    size_t hook_offset;
    void *replacement;
    void *original;
    struct security_hook_list *entry;
    /* true = INSERT our own hook instead of overwriting the slot of an entry resolved by
     * symbol name (see lsm_hook.c).  Nothing is resolved by name, so target_name stays
     * unused, and what happens depends on the version:
     *
     *   < 6.12  our own struct security_hook_list node goes at the HEAD of the hook's
     *           list, so it runs BEFORE every registered LSM (SELinux included): it can
     *           only ADD a denial and can never suppress another LSM's decision, and
     *           `original` stays NULL - the replacement returns 0 on the pass-through
     *           path and must not call anything.
     *   >= 6.12 the SELinux slot of the hook in static_calls_table is taken over (found
     *           by identity, see ksu_lsm_hook_insert_scall()), so the replacement runs
     *           at SELinux's position: `original` IS set to the function that was
     *           there, and a pass-through has to call it or SELinux's decision for that
     *           hook is dropped for every call. */
    bool insert;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
    struct lsm_static_call *scall;
#else
    struct security_hook_list list;
#endif
    /* the offset from target_name to the real hook target */
    int offset;
};

#define KSU_LSM_HOOK_INIT(member, target_symbol, replacement_fn, off)                                                  \
    {                                                                                                                  \
        .head_name = #member, .target_name = target_symbol, .head_offset = offsetof(KSU_LSM_HOOK_HEADS_TYPE, member),  \
        .hook_offset = offsetof(struct security_hook_list, hook.member), .replacement = (void *)(replacement_fn),      \
        .offset = off,                                                                                                 \
    }

/* Insert-shaped initialiser: no symbol is resolved by name (nothing to find), so there
 * is no target_name and `original` starts as NULL.  head_offset comes from
 * KSU_LSM_HOOK_HEADS_TYPE, i.e. from the member of whichever dispatch table the kernel
 * uses on that version - struct security_hook_heads (< 6.12, where it names the hook's
 * hlist_head and is cross-checked against the kernel's own registration by
 * ksu_lsm_hook_head_at() in lsm_hook.c) and struct lsm_static_calls_table (>= 6.12,
 * where each member is `struct lsm_static_call <member>[MAX_LSM_COUNT]`, so the offset
 * names the first slot of that hook's array and is only the fallback of the identity
 * scan) - which is why one macro serves both dispatch types. */
#define KSU_LSM_HOOK_INSERT(member, replacement_fn)                                          \
    {                                                                                        \
        .head_name = #member, .head_offset = offsetof(KSU_LSM_HOOK_HEADS_TYPE, member),       \
        .hook_offset = offsetof(struct security_hook_list, hook.member),                      \
        .replacement = (void *)(replacement_fn), .insert = true,                              \
    }

/* Runtime patching of existing LSM hook slots (workaround for out-of-tree modules; the normal
 * path is security_add_hooks()), made coherent by text patching + RCU synchronization. */

int ksu_lsm_hook(struct ksu_lsm_hook *hook);
void ksu_lsm_unhook(struct ksu_lsm_hook *hook);

int ksu_register_lsm_hook(struct ksu_lsm_hook *hook);
void ksu_unregister_lsm_hook(struct ksu_lsm_hook *hook);

void ksu_lsm_hook_init(void);
void ksu_lsm_hook_exit(void);

/* Whether the table hook insertion walks (security_hook_heads below 6.12, static_calls_table
 * from 6.12) is resolvable on this kernel.  The layer table makes lsm_hook fatal: without it no
 * sus_path/LSM layer can be inserted, so the module would load but apply nothing. */
bool ksu_lsm_hook_ready(void);

#endif
