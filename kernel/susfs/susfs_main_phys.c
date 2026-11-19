/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_main_phys.c - control-node SELinux labelling (P layer)
 *
 * Moved out of susfs_main.c so the entry file no longer reaches
 * kern_path()/path_put() or the SELinux security struct directly.
 */

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/path.h>
#include <linux/security.h>

#include "symbol_resolver.h"
#include "ksu_allow_gate.h"
#include "susfs_log.h"
#include "btf_layout.h"
#include "lsm_hook.h"
#include "susfs_control.h"
#include "susfs_core.h"
#include "susfs_main_data.h"

/* The control nodes are born with procfs's default label, u:object_r:proc:s0, whose policy refuses
 * even an adb shell that this module's gate accepted (measured: `cat /proc/susfs_open_redirect`
 * from u:r:shell:s0 is EACCES, while the same read from u:r:ksu:s0 works).  ksu_file is the type
 * the KernelSU policy already grants to the processes that may use su - exactly the set these
 * nodes exist for - so they are moved under it.
 *
 * There is no LSM interface for this: security_inode_setsecurity() has no inode_setsecurity hook
 * registered on this kernel (it answers the framework default, -EOPNOTSUPP, for either spelling of
 * the attribute name), and procfs has no xattr handler to go through the VFS.  What is left is the
 * field SELinux itself reads: inode->i_security points at its private inode_security_struct (until
 * the per-LSM blob split in 6.4), and the `sid` member is what every check uses.  The offset comes
 * from BTF and the member's type is verified, so a kernel whose layout moved it is refused instead
 * of being written at a guess.
 *
 * This runs once at load, on inodes nobody has opened yet; a plain 32-bit store needs no lock. */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 4, 0)
static const char susfs_control_selinux_ctx[] = "u:object_r:ksu_file:s0";
static const char susfs_proc_selinux_ctx[] = "u:object_r:proc:s0";
static size_t susfs_isec_base_offset;	/* SELinux's slot inside inode->i_security */
static size_t susfs_isec_sid_offset;
static u32 susfs_control_sid;
static u32 susfs_proc_sid;

static bool susfs_prepare_control_sid(void)
{
    unsigned long blob_sizes;
    size_t lbs_inode_off;
    int sel_off;
    int rc;

    if (security_secctx_to_secid(susfs_control_selinux_ctx,
                                 strlen(susfs_control_selinux_ctx),
                                 &susfs_control_sid) ||
        !susfs_control_sid)
        return false;
    if (security_secctx_to_secid(susfs_proc_selinux_ctx,
                                 strlen(susfs_proc_selinux_ctx),
                                 &susfs_proc_sid) ||
        !susfs_proc_sid)
        return false;
    /* Idempotent: the fsnotify layer has usually called this already. */
    if (susfs_btf_layout_init())
        return false;
    /* Every Android 5.15 here carries the per-LSM blob allocation even though the split landed
     * upstream later: inode->i_security is then a blob whose SELinux slot starts at
     * selinux_blob_sizes.lbs_inode.  A kernel without that symbol is the older shape, where
     * inode->i_security IS the struct and the base stays 0. */
    blob_sizes = find_kernel_symbol_exact("selinux_blob_sizes");
    if (blob_sizes) {
        if (susfs_btf_member_offset("lsm_blob_sizes", "lbs_inode", "int",
                                    &lbs_inode_off))
            return false;
        sel_off = READ_ONCE(*(int *)((char *)blob_sizes + lbs_inode_off));
        if (sel_off < 0 || sel_off > 4096)
            return false;
        susfs_isec_base_offset = (size_t)sel_off;
    }
    rc = susfs_btf_member_offset("inode_security_struct", "sid", "uint",
                                 &susfs_isec_sid_offset);
    return !rc;
}

/* The write is only made where the field already holds the proc label: that is what proves the
 * base, the offset and the blob ownership are all right on the kernel actually running.  Anything
 * else is left alone - a wrong guess here would corrupt a live struct. */
static void susfs_set_inode_sid(struct inode *inode, u32 sid)
{
    u32 *field;

    if (!inode->i_security)
        return;
    field = (u32 *)((char *)inode->i_security + susfs_isec_base_offset +
                    susfs_isec_sid_offset);
    if (READ_ONCE(*field) != susfs_proc_sid)
        return;
    WRITE_ONCE(*field, sid);
}
#endif

void susfs_relabel_control_nodes(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    /* The split is upstream here and the blob walk above is untested there, so the nodes keep their
     * default (proc) label.  That is exactly why the gate must NOT widen here: susfs_control_visible()
     * stays root-only until susfs_control_relabelled is set, because proc's policy answers EACCES to
     * the shell and to su-granted apps - the gate letting them in would turn the nodes' ENOENT into
     * an EACCES that proves they exist (susfs4ksu-lkm#108). */
    return;
#else
    int i, found = 0, changed = 0;

    if (!susfs_expose_proc)
        return;
    /* No KernelSU, no ksu_file label.  That label exists so the KernelSU predicate's set
     * (shell, the manager, su-granted apps) may read the nodes; without the predicate there is
     * nobody to widen to, so the nodes keep proc's own label and the gate stays the original
     * root-only one.  susfs_prepare_control_sid() would otherwise succeed here on BTF alone -
     * a Magisk device has no KernelSU predicate but does have SELinux - and stamp a label this
     * build has no use for. */
    if (READ_ONCE(susfs_ksu_allow_kind) != 1) {
        SUSFS_LOGI("susfs: no KernelSU predicate - control nodes keep proc's label, gate stays root-only\n");
        return;
    }
    if (!susfs_prepare_control_sid()) {
        SUSFS_LOGI("susfs: no ksu_file label applied (context or BTF layout unavailable)\n");
        return;
    }
    for (i = 0; i < susfs_self_hide_paths_count; i++) {
        struct path p;
        struct inode *inode;

        if (kern_path(susfs_self_hide_paths[i], 0, &p))
            continue;
        found++;
        inode = d_inode(p.dentry);
        if (inode && inode->i_security) {
            u32 *field = (u32 *)((char *)inode->i_security +
                                 susfs_isec_base_offset +
                                 susfs_isec_sid_offset);

            if (READ_ONCE(*field) == susfs_proc_sid) {
                susfs_set_inode_sid(inode, susfs_control_sid);
                changed++;
            }
        }
        path_put(&p);
    }
    SUSFS_LOGI("susfs: %d of %d control node(s) relabelled to %s\n",
               changed, found, susfs_control_selinux_ctx);
    /* Only a node that is actually relabelled may be widened to the KernelSU set: a node left under
     * proc's label answers EACCES to exactly the callers the wider gate just admitted, and EACCES
     * says "it is here" where ENOENT says "nothing here".  All-or-nothing on purpose - a partial
     * relabel means the set is not usable, and root-only is what those labels say anyway. */
    WRITE_ONCE(susfs_control_relabelled, found > 0 && changed == found);
#endif
}

/* ---- layer table: arming order IS the teardown order, reversed --------------
 * A failed load must leave NOTHING armed: when module_init() returns an error the kernel
 * frees this module's memory, so anything still installed - a patched security_hook_heads
 * slot above all - becomes a function pointer into freed memory that the next syscall goes
 * through.  Hence the exits of the already-armed layers run before failing.
 * Teardown runs in the exact reverse of arming, in one place, and the order matters:
 * sus_path's LSM layer is what answers ENOENT for this module's own 0777 control nodes (see
 * the expose_proc note), so unhooking it before the nodes are removed would expose them to
 * every process during the unload - here the nodes go first and the LSM layer last.
 * The load plan marks four layers as fatal: fp_hook, lsm_hook, sus_path and supercall; the
 * rest log their own failure and degrade to "off".  The exits are idempotent, which lets one
 * path serve rollback and unload alike. */
