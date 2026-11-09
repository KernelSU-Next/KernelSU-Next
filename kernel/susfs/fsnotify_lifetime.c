/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/wait.h>

#include "btf_layout.h"
#include "fsnotify_lifetime.h"
#include "symbol_resolver.h"

struct fsnotify_group;
struct fsnotify_mark;
struct fsnotify_ops;
struct fsnotify_mark_connector;

struct susfs_sb_watch {
    struct list_head list;
    struct super_block *sb;
    unsigned char mark[] __aligned(sizeof(void *));
};

static DEFINE_MUTEX(susfs_watch_lock);
static LIST_HEAD(susfs_watches);
static DECLARE_WAIT_QUEUE_HEAD(susfs_watch_wait);
static atomic_t susfs_watch_count = ATOMIC_INIT(0);
static struct fsnotify_group *susfs_watch_group;
static void *susfs_watch_ops;
static void (*susfs_watch_release)(struct super_block *);
static size_t susfs_mark_size;
static size_t susfs_sb_marks_offset;
static unsigned int susfs_sb_object_type;
static bool susfs_alloc_with_flags;
static bool susfs_add_with_object;
static bool susfs_watch_stopping;
static void *susfs_alloc_group_fn;
static void *susfs_add_mark_fn;
static void (*susfs_init_mark_fn)(struct fsnotify_mark *, struct fsnotify_group *);
static void (*susfs_put_mark_fn)(struct fsnotify_mark *);
static void (*susfs_destroy_group_fn)(struct fsnotify_group *);
static void (*susfs_wait_marks_fn)(void);

static struct fsnotify_group *__nocfi susfs_alloc_group(void)
{
    if (susfs_alloc_with_flags)
        return ((struct fsnotify_group *(*)(const struct fsnotify_ops *, int))
                    susfs_alloc_group_fn)(susfs_watch_ops, 0);
    return ((struct fsnotify_group *(*)(const struct fsnotify_ops *))
                susfs_alloc_group_fn)(susfs_watch_ops);
}

static void __nocfi susfs_init_mark(struct fsnotify_mark *mark)
{
    susfs_init_mark_fn(mark, susfs_watch_group);
}

static int __nocfi susfs_add_mark(struct fsnotify_mark *mark, struct super_block *sb)
{
    if (susfs_add_with_object)
        return ((int (*)(struct fsnotify_mark *, void *, unsigned int, int))
                    susfs_add_mark_fn)(mark, sb, susfs_sb_object_type, 0);
    return ((int (*)(struct fsnotify_mark *, struct fsnotify_mark_connector **,
                    unsigned int, int, void *))susfs_add_mark_fn)
        (mark, (void *)((char *)sb + susfs_sb_marks_offset),
         susfs_sb_object_type, 0, NULL);
}

static void __nocfi susfs_put_mark(struct fsnotify_mark *mark)
{
    susfs_put_mark_fn(mark);
}

static void __nocfi susfs_destroy_group(struct fsnotify_group *group)
{
    susfs_destroy_group_fn(group);
}

static void __nocfi susfs_wait_marks(void)
{
    susfs_wait_marks_fn();
}

static struct susfs_sb_watch *susfs_watch_from_mark(struct fsnotify_mark *mark)
{
    return (void *)((char *)mark - offsetof(struct susfs_sb_watch, mark));
}

static void susfs_watch_freeing(struct fsnotify_mark *mark,
                               struct fsnotify_group *group)
{
    struct susfs_sb_watch *watch = susfs_watch_from_mark(mark);
    struct super_block *sb = watch->sb;

    mutex_lock(&susfs_watch_lock);
    list_del_init(&watch->list);
    mutex_unlock(&susfs_watch_lock);
    susfs_watch_release(sb);
    susfs_put_mark(mark);
}

static void susfs_watch_free(struct fsnotify_mark *mark)
{
    kfree(susfs_watch_from_mark(mark));
    atomic_dec(&susfs_watch_count);
    wake_up_all(&susfs_watch_wait);
}

static int susfs_watch_abi(size_t *ops_size, size_t *freeing_offset,
                         size_t *free_offset)
{
    static const char *const alloc_one[] = { "fsnotify_ops*" };
    static const char *const alloc_two[] = { "fsnotify_ops*", "int" };
    static const char *const add_four[] = {
        "fsnotify_mark*", "void*", "uint", "int"
    };
    static const char *const add_five[] = {
        "fsnotify_mark*", "fsnotify_mark_connector**", "uint", "int",
        "__kernel_fsid_t*"
    };
    static const char *const mark_group[] = { "fsnotify_mark*", "fsnotify_group*" };
    static const char *const mark_one[] = { "fsnotify_mark*" };
    static const char *const group_one[] = { "fsnotify_group*" };
    int object_type;
    int ret;

    ret = susfs_btf_layout_init();
    if (ret)
        return ret;
    ret = susfs_btf_struct_size("fsnotify_mark", &susfs_mark_size);
    if (ret)
        return ret;
    ret = susfs_btf_struct_size("fsnotify_ops", ops_size);
    if (ret)
        return ret;
    if (susfs_mark_size < sizeof(void *) || susfs_mark_size % sizeof(void *) ||
        *ops_size < 2 * sizeof(void *) || *ops_size % sizeof(void *))
        return -EINVAL;
    ret = susfs_btf_callback_offset("fsnotify_ops", "freeing_mark", "void",
                                    mark_group, ARRAY_SIZE(mark_group), freeing_offset);
    if (ret)
        return ret;
    ret = susfs_btf_callback_offset("fsnotify_ops", "free_mark", "void",
                                    mark_one, ARRAY_SIZE(mark_one), free_offset);
    if (ret)
        return ret;
    if (*freeing_offset == *free_offset)
        return -EINVAL;
    ret = susfs_btf_enum_value("fsnotify_obj_type", "FSNOTIFY_OBJ_TYPE_SB", &object_type);
    if (ret)
        return ret;
    if (object_type < 0 || object_type > 16)
        return -EINVAL;
    susfs_sb_object_type = object_type;
    ret = susfs_btf_check_function("fsnotify_alloc_group", "fsnotify_group*",
                                   alloc_two, ARRAY_SIZE(alloc_two));
    susfs_alloc_with_flags = !ret;
    if (ret) {
        ret = susfs_btf_check_function("fsnotify_alloc_group", "fsnotify_group*",
                                       alloc_one, ARRAY_SIZE(alloc_one));
        if (ret)
            return ret;
    }
    ret = susfs_btf_check_function("fsnotify_add_mark", "int",
                                   add_four, ARRAY_SIZE(add_four));
    susfs_add_with_object = !ret;
    if (ret) {
        ret = susfs_btf_check_function("fsnotify_add_mark", "int",
                                       add_five, ARRAY_SIZE(add_five));
        if (ret)
            return ret;
        ret = susfs_btf_field_offset("super_block", "s_fsnotify_marks",
                                     "fsnotify_mark_connector*", &susfs_sb_marks_offset);
        if (ret)
            return ret;
    }
    ret = susfs_btf_check_function("fsnotify_init_mark", "void",
                                   mark_group, ARRAY_SIZE(mark_group));
    if (ret)
        return ret;
    ret = susfs_btf_check_function("fsnotify_put_mark", "void",
                                   mark_one, ARRAY_SIZE(mark_one));
    if (ret)
        return ret;
    ret = susfs_btf_check_function("fsnotify_destroy_group", "void",
                                   group_one, ARRAY_SIZE(group_one));
    if (ret)
        return ret;
    return susfs_btf_check_function("fsnotify_wait_marks_destroyed", "void", NULL, 0);
}

int susfs_sb_watch_init(void (*release)(struct super_block *))
{
    void (*freeing)(struct fsnotify_mark *, struct fsnotify_group *) = susfs_watch_freeing;
    void (*free_mark)(struct fsnotify_mark *) = susfs_watch_free;
    size_t ops_size, freeing_offset, free_offset;
    int ret;

    if (!release)
        return -EINVAL;
    if (susfs_watch_group)
        return -EALREADY;
    ret = susfs_watch_abi(&ops_size, &freeing_offset, &free_offset);
    if (ret) {
        pr_err("sus_path: fsnotify lifetime ABI validation failed: %d\n", ret);
        return ret;
    }
    susfs_alloc_group_fn = ksu_resolve_symbol_for_functable_hook("fsnotify_alloc_group");
    susfs_add_mark_fn = ksu_resolve_symbol_for_functable_hook("fsnotify_add_mark");
    susfs_init_mark_fn = ksu_resolve_symbol_for_functable_hook("fsnotify_init_mark");
    susfs_put_mark_fn = ksu_resolve_symbol_for_functable_hook("fsnotify_put_mark");
    susfs_destroy_group_fn = ksu_resolve_symbol_for_functable_hook("fsnotify_destroy_group");
    susfs_wait_marks_fn = ksu_resolve_symbol_for_functable_hook("fsnotify_wait_marks_destroyed");
    if (!susfs_alloc_group_fn || !susfs_add_mark_fn || !susfs_init_mark_fn ||
        !susfs_put_mark_fn || !susfs_destroy_group_fn || !susfs_wait_marks_fn)
        return -EOPNOTSUPP;
    susfs_watch_ops = kzalloc(ops_size, GFP_KERNEL);
    if (!susfs_watch_ops)
        return -ENOMEM;
    memcpy((char *)susfs_watch_ops + freeing_offset, &freeing, sizeof(freeing));
    memcpy((char *)susfs_watch_ops + free_offset, &free_mark, sizeof(free_mark));
    susfs_watch_group = susfs_alloc_group();
    if (IS_ERR_OR_NULL(susfs_watch_group)) {
        ret = susfs_watch_group ? PTR_ERR(susfs_watch_group) : -ENOMEM;
        susfs_watch_group = NULL;
        kfree(susfs_watch_ops);
        susfs_watch_ops = NULL;
        return ret;
    }
    susfs_watch_release = release;
    susfs_watch_stopping = false;
    pr_info("sus_path: fsnotify lifetime ready (mark=%zu, ops=%zu, SB=%u)\n",
            susfs_mark_size, ops_size, susfs_sb_object_type);
    return 0;
}

int susfs_sb_watch_add(struct super_block *sb)
{
    struct susfs_sb_watch *watch;
    struct fsnotify_mark *mark;
    unsigned int nofs;
    int ret = 0;

    if (!sb)
        return -EINVAL;
    mutex_lock(&susfs_watch_lock);
    if (!susfs_watch_group || susfs_watch_stopping) {
        ret = -ESHUTDOWN;
        goto out_unlock;
    }
    list_for_each_entry(watch, &susfs_watches, list) {
        if (watch->sb == sb)
            goto out_unlock;
    }
    nofs = memalloc_nofs_save();
    watch = kzalloc(sizeof(*watch) + susfs_mark_size, GFP_KERNEL);
    if (!watch) {
        ret = -ENOMEM;
        goto out_restore;
    }
    INIT_LIST_HEAD(&watch->list);
    watch->sb = sb;
    mark = (void *)watch->mark;
    atomic_inc(&susfs_watch_count);
    susfs_init_mark(mark);
    ret = susfs_add_mark(mark, sb);
    if (ret) {
        susfs_put_mark(mark);
        goto out_restore;
    }
    list_add_tail(&watch->list, &susfs_watches);
out_restore:
    memalloc_nofs_restore(nofs);
out_unlock:
    mutex_unlock(&susfs_watch_lock);
    return ret;
}

void susfs_sb_watch_exit(void)
{
    struct fsnotify_group *group;

    mutex_lock(&susfs_watch_lock);
    susfs_watch_stopping = true;
    group = susfs_watch_group;
    mutex_unlock(&susfs_watch_lock);
    if (!group)
        return;
    susfs_destroy_group(group);
    wait_event(susfs_watch_wait, !atomic_read(&susfs_watch_count));
    susfs_wait_marks();
    mutex_lock(&susfs_watch_lock);
    susfs_watch_group = NULL;
    susfs_watch_release = NULL;
    kfree(susfs_watch_ops);
    susfs_watch_ops = NULL;
    mutex_unlock(&susfs_watch_lock);
}
