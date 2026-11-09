#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/namei.h>
#include <linux/uaccess.h>
#include <linux/cred.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/ptrace.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/kthread.h>

#include "policy/feature.h"
#include "ksu.h"
#include "uapi/feature.h"
#include "selinux/selinux.h"
#include "feature/selinux_hide.h"
#include "compat/kernel_compat.h"

#ifndef __nocfi
#define __nocfi
#endif

#ifdef KSU_COMPAT_USE_SELINUX_STATE
extern struct selinux_state selinux_state;
#define KSU_HAS_SELINUX_STATE 1
#else
#define KSU_HAS_SELINUX_STATE 0
#endif

#ifndef SIMPLE_TRANSACTION_LIMIT
#define SIMPLE_TRANSACTION_LIMIT (PAGE_SIZE - 1)
#endif

#define KSU_SEL_CONTEXT_INO 5
#define KSU_SEL_ACCESS_INO  6

static bool ksu_selinux_hide_enabled __read_mostly = true;

struct ksu_hide_buf {
    size_t len;
    char   data[];
} __aligned(sizeof(size_t));

static struct ksu_hide_buf __rcu *ksu_hide_type_list = NULL;
static struct ksu_hide_buf __rcu *ksu_hide_rule_list = NULL;
static DEFINE_MUTEX(ksu_hide_list_mutex);

static void ksu_rcu_replace(struct ksu_hide_buf __rcu **slot, struct ksu_hide_buf *new_buf)
{
    struct ksu_hide_buf *old = rcu_dereference_protected(*slot, 1);
    rcu_assign_pointer(*slot, new_buf);
    synchronize_rcu();
    kfree(old);
}

void ksu_selinux_hide_add_type(const char *name)
{
    struct ksu_hide_buf *old_buf, *new_buf;
    size_t old_len, need, i, name_len;

    if (!name || !*name)
        return;

    name_len = strlen(name);
    need = name_len + 3;

    mutex_lock(&ksu_hide_list_mutex);
    old_buf = rcu_dereference_protected(ksu_hide_type_list, 1);
    old_len = old_buf ? old_buf->len : 0;

    if (old_buf) {
        for (i = 0; i < old_len; ) {
            const char *cur = old_buf->data + i;
            if (!strncmp(cur + 1, name, name_len) && cur[name_len + 1] == ':')
                goto out;
            i += strlen(cur) + 1;
        }
    }

    new_buf = kmalloc(sizeof(*new_buf) + old_len + need, GFP_KERNEL);
    if (!new_buf)
        goto out;
    new_buf->len = old_len + need;
    if (old_buf && old_len)
        memcpy(new_buf->data, old_buf->data, old_len);
    sprintf(new_buf->data + old_len, ":%s:", name);

    ksu_rcu_replace(&ksu_hide_type_list, new_buf);
    pr_info_ratelimited("selinux_hide: track type: :%s:\n", name);

out:
    mutex_unlock(&ksu_hide_list_mutex);
}

void ksu_selinux_hide_add_rule(const char *src, const char *tgt)
{
    struct ksu_hide_buf *old_buf, *new_buf;
    size_t old_len, need_src, need_tgt, need, i;
    char *p;

    if (!src || !*src || !tgt || !*tgt)
        return;

    need_src = strlen(src) + 3;
    need_tgt = strlen(tgt) + 3;
    need     = need_src + need_tgt;

    mutex_lock(&ksu_hide_list_mutex);
    old_buf = rcu_dereference_protected(ksu_hide_rule_list, 1);
    old_len = old_buf ? old_buf->len : 0;

    if (old_buf) {
        for (i = 0; i < old_len; ) {
            const char *csrc = old_buf->data + i;
            size_t ssz = strlen(csrc) + 1;
            const char *ctgt = csrc + ssz;
            size_t tsz = strlen(ctgt) + 1;

            if (!strcmp(csrc + 1, src) && !strcmp(ctgt + 1, tgt))
                goto out;
            i += ssz + tsz;
        }
    }

    new_buf = kmalloc(sizeof(*new_buf) + old_len + need, GFP_KERNEL);
    if (!new_buf)
        goto out;
    new_buf->len = old_len + need;
    if (old_buf && old_len)
        memcpy(new_buf->data, old_buf->data, old_len);

    p = new_buf->data + old_len;
    sprintf(p, ":%s:", src);
    p += strlen(p) + 1;
    sprintf(p, ":%s:", tgt);

    ksu_rcu_replace(&ksu_hide_rule_list, new_buf);
    pr_info_ratelimited("selinux_hide: track rule: :%s: -> :%s:\n", src, tgt);

out:
    mutex_unlock(&ksu_hide_list_mutex);
}

static bool ksu_should_hide_context(const char *str)
{
    struct ksu_hide_buf *buf;
    size_t off;

    if (!str)
        return false;

    rcu_read_lock();

    buf = rcu_dereference(ksu_hide_type_list);
    if (buf) {
        size_t len = buf->len;
        off = 0;
        while (off < len) {
            const char *entry = buf->data + off;
            if (strstr(str, entry)) {
                rcu_read_unlock();
                return true;
            }
            off += strlen(entry) + 1;
        }
    }

    buf = rcu_dereference(ksu_hide_rule_list);
    if (buf) {
        size_t len = buf->len;
        off = 0;
        while (off < len) {
            const char *src = buf->data + off;
            size_t ssz = strlen(src) + 1;
            const char *tgt = src + ssz;
            size_t tsz = strlen(tgt) + 1;

            if (strstr(str, src) && strstr(str, tgt)) {
                rcu_read_unlock();
                return true;
            }
            off += ssz + tsz;
        }
    }

    rcu_read_unlock();
    return false;
}

struct ksu_dirty_rule {
    const char *scon;
    const char *tcon;
};

static const struct ksu_dirty_rule ksu_dirty_rules[] = {
    { "u:r:shell:s0",         "u:r:su:s0"                   },
    { "u:r:adbd:s0",          "u:r:adbroot:s0"              },
    { "u:r:zygote:s0",        "u:object_r:adb_data_file:s0" },
    { "u:r:magisk:s0",        "u:r:droidspacesd:s0"         },
    { "u:r:su:s0",            "u:r:droidspacesd:s0"         },
    { "u:r:system_server:s0", "u:r:droidspacesd:s0"         },
};

static bool ksu_is_dirty_access_query(const char *scon, const char *tcon)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(ksu_dirty_rules); i++) {
        const struct ksu_dirty_rule *r = &ksu_dirty_rules[i];
        if (!strcmp(scon, r->scon) && !strcmp(tcon, r->tcon))
            return true;
    }
    return false;
}

static void ksu_fix_access_response(void *private_data, bool deny)
{
    struct simple_transaction_argresp *ar = private_data;
    unsigned int allowed, ff, auditallow, auditdeny, flags, seqno;
    char buf[64];
    int len;

    if (!ar)
        return;

    if (sscanf(ar->data, "%x %x %x %x %u %x",
               &allowed, &ff, &auditallow, &auditdeny, &seqno, &flags) != 6)
        return;

    if (deny) {
        allowed    = 0;
        auditallow = 0;
        auditdeny  = 0xffffffff;
        flags      = 0;
    }

    seqno = 1;

    len = scnprintf(buf, sizeof(buf), "%x %x %x %x %u %x",
                    allowed, 0xffffffff, auditallow, auditdeny, seqno, flags);

    if (len > 0 && len < (int)SIMPLE_TRANSACTION_LIMIT) {
        memcpy(ar->data, buf, len);
        ar->data[len] = '\0';
        ar->size = len;
    }
}

static int ksu_patch_fops_slot(void **slot, void *new_fn)
{
    unsigned long addr   = (unsigned long)slot;
    unsigned long base   = addr & PAGE_MASK;
    unsigned long offset = addr & ~PAGE_MASK;
    struct page *page    = phys_to_page(__pa(base));
    void *w;

    if (!page)
        return -EFAULT;

    w = vmap(&page, 1, VM_MAP, PAGE_KERNEL);
    if (!w)
        return -ENOMEM;

    WRITE_ONCE(*(void **)((unsigned long)w + offset), new_fn);
    smp_mb();
    vunmap(w);
    return 0;
}

static struct file_operations *ksu_resolve_fops(const char *path_str)
{
    struct path path;
    struct file_operations *fops = NULL;

    if (kern_path(path_str, LOOKUP_FOLLOW, &path))
        return NULL;
    if (path.dentry && d_inode(path.dentry))
        fops = (struct file_operations *)d_inode(path.dentry)->i_fop;
    path_put(&path);
    return fops;
}

static ssize_t (*orig_sel_write)(struct file *, const char __user *, size_t, loff_t *);
static struct file_operations *g_sel_context_fops;

static __nocfi ssize_t ksu_sel_write(struct file *file, const char __user *ubuf, size_t size, loff_t *pos)
{
    ino_t ino;
    char kbuf[256];
    size_t len;
    ssize_t ret;

    if (unlikely(!ksu_selinux_hide_enabled))
        goto passthrough;
    if (current_uid().val < 10000)
        goto passthrough;

    ino = file_inode(file)->i_ino;
    if (ino != KSU_SEL_CONTEXT_INO && ino != KSU_SEL_ACCESS_INO)
        goto passthrough;

    len = size < sizeof(kbuf) - 1 ? size : sizeof(kbuf) - 1;
    if (copy_from_user(kbuf, ubuf, len))
        goto passthrough;
    kbuf[len] = 0;

    if (ino == KSU_SEL_CONTEXT_INO) {
        if (ksu_should_hide_context(kbuf)) {
            pr_info_ratelimited("selinux_hide: blocked context: %.40s\n", kbuf);
            return -EINVAL;
        }
        goto passthrough;
    }

    ret = orig_sel_write(file, ubuf, size, pos);
    if (ret <= 0)
        return ret;

    {
        bool deny = false;
        char scon[128], tcon[128];
        unsigned int tclass_num;

        if (ksu_should_hide_context(kbuf))
            deny = true;

        if (sscanf(kbuf, "%127s %127s %u", scon, tcon, &tclass_num) == 3 &&
            ksu_is_dirty_access_query(scon, tcon))
            deny = true;

        ksu_fix_access_response(file->private_data, deny);
    }

    return ret;

passthrough:
    return orig_sel_write(file, ubuf, size, pos);
}

static ssize_t (*orig_proc_pid_attr_write)(struct file *, const char __user *, size_t, loff_t *) __read_mostly;
static struct file_operations *g_proc_attr_fops;

static __nocfi ssize_t ksu_proc_pid_attr_write(struct file *file, const char __user *ubuf, size_t size, loff_t *pos)
{
    char kbuf[128];
    size_t len;
    const char *attr_name;

    if (unlikely(!ksu_selinux_hide_enabled))
        goto passthrough;
    if (current_uid().val < 10000)
        goto passthrough;

    attr_name = file->f_path.dentry->d_name.name;
    if (!attr_name || strcmp(attr_name, "current"))
        goto passthrough;

    len = size < sizeof(kbuf) - 1 ? size : sizeof(kbuf) - 1;
    if (copy_from_user(kbuf, ubuf, len))
        goto passthrough;
    kbuf[len] = '\0';

    if (ksu_should_hide_context(kbuf)) {
        pr_info_ratelimited("selinux_hide: attr/current blocked: %.40s\n", kbuf);
        return -EINVAL;
    }

passthrough:
    return orig_proc_pid_attr_write(file, ubuf, size, pos);
}

static void ksu_hook_proc_attr_write(void)
{
    struct file_operations *fops;
    int ret;

    if (orig_proc_pid_attr_write)
        return;

    fops = ksu_resolve_fops("/proc/self/attr/current");
    if (!fops || !fops->write)
        return;

    g_proc_attr_fops = fops;
    orig_proc_pid_attr_write = fops->write;
    ret = ksu_patch_fops_slot((void **)&fops->write, ksu_proc_pid_attr_write);
    if (ret)
        pr_warn("selinux_hide: proc_attr hook failed: %d\n", ret);
}

static void ksu_hook_transaction_write(void)
{
    struct file_operations *fops;
    int ret;

    if (orig_sel_write)
        return;

    fops = ksu_resolve_fops("/sys/fs/selinux/context");
    if (!fops || !fops->write)
        return;

    g_sel_context_fops = fops;
    orig_sel_write = fops->write;
    ret = ksu_patch_fops_slot((void **)&fops->write, ksu_sel_write);
    if (ret)
        pr_warn("selinux_hide: patch write failed: %d\n", ret);
}

static struct page *ksu_fake_status_page __read_mostly = NULL;
static int (*orig_status_open)(struct inode *, struct file *) = NULL;
static struct file_operations *g_status_fops;

static struct page *ksu_get_real_status_page(void)
{
#if KSU_HAS_SELINUX_STATE
    return selinux_kernel_status_page(&selinux_state);
#else
    return selinux_kernel_status_page();
#endif
}

static int ksu_prepare_fake_status_page(void)
{
    struct page *real, *fake;
    struct selinux_kernel_status *rs, *fs;

    if (READ_ONCE(ksu_fake_status_page))
        return 0;

    real = ksu_get_real_status_page();
    if (!real)
        return -ENOENT;

    fake = alloc_page(GFP_KERNEL | __GFP_ZERO);
    if (!fake)
        return -ENOMEM;

    rs = page_address(real);
    fs = page_address(fake);
    memcpy(fs, rs, sizeof(*fs));

    fs->enforcing = 1;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
    fs->sequence    = 4;
    fs->policyload  = 1;
#else
    fs->sequence    = 0;
    fs->policyload  = 0;
#endif

    smp_store_release(&ksu_fake_status_page, fake);
    pr_info_ratelimited("selinux_hide: fake status ready seq=%d\n", fs->sequence);
    return 0;
}

static __nocfi int ksu_status_open(struct inode *inode, struct file *filp)
{
    if (likely(ksu_selinux_hide_enabled && current_uid().val >= 10000)) {
        struct page *fp = smp_load_acquire(&ksu_fake_status_page);
        if (fp) {
            filp->private_data = fp;
            return 0;
        }
    }
    return orig_status_open(inode, filp);
}

static void ksu_hook_status_open(void)
{
    struct file_operations *fops;
    int ret;

    if (orig_status_open)
        return;

    fops = ksu_resolve_fops("/sys/fs/selinux/status");
    if (!fops || !fops->open)
        return;

    g_status_fops = fops;
    orig_status_open = fops->open;
    ret = ksu_patch_fops_slot((void **)&fops->open, ksu_status_open);
    if (ret)
        pr_warn("selinux_hide: patch status open failed: %d\n", ret);
}

static int selinux_hide_get(u64 *v)
{
    *v = ksu_selinux_hide_enabled ? 1 : 0;
    return 0;
}

static int selinux_hide_set(u64 v)
{
    ksu_selinux_hide_enabled = !!v;
    pr_info_ratelimited("selinux_hide: enabled=%d\n", ksu_selinux_hide_enabled);
    return 0;
}

static const struct ksu_feature_handler selinux_hide_handler = {
    .feature_id = KSU_FEATURE_SELINUX_HIDE,
    .name = "selinux_hide",
    .get_handler = selinux_hide_get,
    .set_handler = selinux_hide_set,
};

static int ksu_selinux_hide_init_thread(void *data)
{
    int tries = 0;

    set_user_nice(current, 19);

    ksu_selinux_hide_add_type(KERNEL_SU_DOMAIN);
    ksu_selinux_hide_add_type(KERNEL_SU_FILE);
    ksu_selinux_hide_add_rule(KERNEL_SU_DOMAIN, KERNEL_SU_FILE);
    ksu_selinux_hide_add_type("magisk");
    ksu_selinux_hide_add_type("magisk_file");
    ksu_selinux_hide_add_type("droidspacesd");
    ksu_selinux_hide_add_type("droidspaces");
    ksu_selinux_hide_add_type("lsposed_file");
    ksu_selinux_hide_add_type("xposed_data");
    ksu_selinux_hide_add_type("msd_app");
    ksu_selinux_hide_add_type("msd_daemon");

try_again:
    if (ksu_prepare_fake_status_page()) {
        if (++tries > 10)
            goto skip_status;
        msleep(1000);
        goto try_again;
    }
    ksu_hook_status_open();

skip_status:
    ksu_hook_transaction_write();
    ksu_hook_proc_attr_write();
    return 0;
}

void ksu_selinux_hide_handle_second_stage(void)
{
    if (!READ_ONCE(ksu_fake_status_page))
        ksu_prepare_fake_status_page();
    ksu_hook_status_open();
    ksu_hook_transaction_write();
    ksu_hook_proc_attr_write();
}

void ksu_selinux_hide_handle_post_fs_data(void)
{
    if (!READ_ONCE(ksu_fake_status_page))
        ksu_prepare_fake_status_page();
    ksu_hook_status_open();
    ksu_hook_transaction_write();
    ksu_hook_proc_attr_write();
}

void __init ksu_selinux_hide_init(void)
{
    if (ksu_register_feature_handler(&selinux_hide_handler))
        pr_err("selinux_hide: register feature failed\n");

    kthread_run(ksu_selinux_hide_init_thread, NULL, "ksu_selinux_hide");
}

void __exit ksu_selinux_hide_exit(void)
{
    ksu_unregister_feature_handler(KSU_FEATURE_SELINUX_HIDE);

    mutex_lock(&ksu_hide_list_mutex);
    ksu_rcu_replace(&ksu_hide_type_list, NULL);
    ksu_rcu_replace(&ksu_hide_rule_list, NULL);
    mutex_unlock(&ksu_hide_list_mutex);

    if (g_sel_context_fops && orig_sel_write)
        ksu_patch_fops_slot((void **)&g_sel_context_fops->write, orig_sel_write);
    if (g_status_fops && orig_status_open)
        ksu_patch_fops_slot((void **)&g_status_fops->open, orig_status_open);
    if (g_proc_attr_fops && orig_proc_pid_attr_write) {
        ksu_patch_fops_slot((void **)&g_proc_attr_fops->write, orig_proc_pid_attr_write);
        orig_proc_pid_attr_write = NULL;
    }
}
