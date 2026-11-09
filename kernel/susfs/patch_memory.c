/* SPDX-License-Identifier: GPL-2.0-only */
/* patch_memory.c - arbitrary kernel address modification (ported from KernelSU/SukiSU hook/arm64/patch_memory.c).
 * Copyright (C) 2023 bmax121. All Rights Reserved.
 * Rewrites read-only kernel memory (STRICT_KERNEL_RWX) via phys_from_virt + fixmap + stop_machine, handling 2MB
 * section mappings (leaf pmd/pud/p4d); each store goes through copy_to_kernel_nofault(), so a mapping the walk
 * got wrong returns an error instead of faulting, and stop_machine parks every online CPU during the write. */

#ifdef __aarch64__

#include "patch_memory.h"
#include "susfs_log.h"
#include "symbol_resolver.h"
#include <linux/cpumask.h>
#include <linux/gfp.h>
#include <linux/uaccess.h>
#include <linux/stop_machine.h>
#include <asm/cacheflush.h>
#include <asm-generic/fixmap.h>

/* Translate a kernel virtual address to a physical address by walking the init_mm page tables (section/leaf
 * mappings at p4d/pud/pmd included).  Returns the physical address, or 0 and sets *err on failure. */
unsigned long phys_from_virt(unsigned long addr, int *err)
{
    struct mm_struct *mm = &init_mm;
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;

    *err = 0;

    pgd = pgd_offset(mm, addr);
    if (pgd_none(*pgd) || pgd_bad(*pgd))
        goto fail;

    p4d = p4d_offset(pgd, addr);
    if (p4d_none(*p4d) || p4d_bad(*p4d))
        goto fail;
#if defined(p4d_leaf)
    if (p4d_leaf(*p4d))
        return __p4d_to_phys(*p4d) + (addr & ~P4D_MASK);
#endif

    pud = pud_offset(p4d, addr);
    if (pud_none(*pud) || pud_bad(*pud))
        goto fail;
#if defined(pud_leaf)
    if (pud_leaf(*pud))
        return __pud_to_phys(*pud) + (addr & ~PUD_MASK);
#endif

    pmd = pmd_offset(pud, addr);
#if defined(pmd_leaf)
    if (pmd_leaf(*pmd))
        return __pmd_to_phys(*pmd) + (addr & ~PMD_MASK);
#endif
    if (pmd_none(*pmd) || pmd_bad(*pmd))
        goto fail;

    pte = pte_offset_kernel(pmd, addr);
    if (!pte || !pte_present(*pte))
        goto fail;

    return __pte_to_phys(*pte) + (addr & ~PAGE_MASK);

fail:
    *err = -ENOENT;
    return 0;
}

/* dcache/icache flush: 5.14 replaced __flush_dcache_area/__flush_icache_range with
 * dcache_clean_inval_poc/caches_clean_inval_pou.  Which spelling a kernel carries is NOT decided by
 * its version number - a vendor kernel can keep the old one (measured: a Samsung 5.x kernel has no
 * dcache_clean_inval_poc, and a module built from 5.14+ headers fails to load there with "Unknown
 * symbol dcache_clean_inval_poc").  Both spellings are therefore resolved by name at run time, new
 * first, and a kernel carrying neither loses patching (cleanly: ksu_patch_text_nosync() refuses)
 * instead of patching without the flush the write needs.  The two dcache helpers also differ in
 * signature: the new one takes (start, end), the old one (addr, size). */
static void (*ksu_dcache_new_fn)(unsigned long start, unsigned long end);
static void (*ksu_dcache_old_fn)(void *addr, size_t size);
static void (*ksu_icache_fn)(unsigned long start, unsigned long end);

/* Resolve them once, from susfs_init() after the symbol resolver is up and before any layer can
 * patch.  Returns 0 when patching is possible. */
int ksu_patch_memory_init(void)
{
    ksu_dcache_new_fn = (void *)find_kernel_symbol_exact("dcache_clean_inval_poc");
    ksu_dcache_old_fn = (void *)find_kernel_symbol_exact("__flush_dcache_area");
    ksu_icache_fn = (void *)find_kernel_symbol_exact("caches_clean_inval_pou");
    if (!ksu_icache_fn)
        ksu_icache_fn = (void *)find_kernel_symbol_exact("__flush_icache_range");

    if (!ksu_dcache_new_fn && !ksu_dcache_old_fn)
        pr_warn("patch_memory: no dcache flush symbol (dcache_clean_inval_poc / __flush_dcache_area) - patching is off\n");
    if (!ksu_icache_fn)
        pr_warn("patch_memory: no icache flush symbol (caches_clean_inval_pou / __flush_icache_range) - an icache flush request would be refused\n");

    /* Only the dcache helper decides whether patching works: every caller today asks for
     * KSU_PATCH_TEXT_FLUSH_DCACHE, and an icache flush would be refused individually below anyway.
     * Reporting "patching is off" for a missing icache name would be wrong on a kernel that patches
     * fine without it. */
    return (!ksu_dcache_new_fn && !ksu_dcache_old_fn) ? -EOPNOTSUPP : 0;
}

/* Both are indirect calls into the kernel, so each carries the __nocfi noinline the rest of the
 * module uses (tools/cfi_sites.py fails the build otherwise). */
static __nocfi noinline void ksu_flush_dcache(unsigned long start, size_t sz)
{
    if (ksu_dcache_new_fn)
        ksu_dcache_new_fn(start, start + sz);
    else if (ksu_dcache_old_fn)
        ksu_dcache_old_fn((void *)start, sz);
}

static __nocfi noinline void ksu_flush_icache(unsigned long start, unsigned long end)
{
    if (ksu_icache_fn)
        ksu_icache_fn(start, end);
}

struct patch_text_info {
    void *dst;
    void *src;
    size_t len;
    atomic_t cpu_count;
    int flags;
};

static int ksu_patch_text_nosync(void *dst, void *src, size_t len, int flags)
{
    unsigned long p = (unsigned long)dst;
    int phy_err;
    unsigned long phy;
    void *map;
    int ret;

    /* Refused before anything is written: a store without its flush leaves other CPUs with a stale
     * copy of the patched word, which is a crash somewhere else entirely. */
    if ((flags & KSU_PATCH_TEXT_FLUSH_ICACHE) && !ksu_icache_fn)
        return -EOPNOTSUPP;
    if ((flags & KSU_PATCH_TEXT_FLUSH_DCACHE) && !ksu_dcache_new_fn && !ksu_dcache_old_fn)
        return -EOPNOTSUPP;

    phy = phys_from_virt(p, &phy_err);
    if (phy_err) {
        pr_err("failed to find phy addr for patch dst 0x%lx\n", p);
        return phy_err;
    }

    map = (void *)set_fixmap_offset(FIX_TEXT_POKE0, phy);
    ret = (int)copy_to_kernel_nofault(map, src, len);
    clear_fixmap(FIX_TEXT_POKE0);

    if (!ret) {
        if (flags & KSU_PATCH_TEXT_FLUSH_ICACHE)
            ksu_flush_icache(p, p + len);
        if (flags & KSU_PATCH_TEXT_FLUSH_DCACHE)
            ksu_flush_dcache(p, len);
    }
    return ret;
}

static int ksu_patch_text_cb(void *arg)
{
    struct patch_text_info *pp = arg;
    int ret = 0;

    if (atomic_inc_return(&pp->cpu_count) == num_online_cpus()) {
        ret = ksu_patch_text_nosync(pp->dst, pp->src, pp->len, pp->flags);
        atomic_inc(&pp->cpu_count);
    } else {
        while (atomic_read(&pp->cpu_count) <= num_online_cpus())
            cpu_relax();
        isb();
    }
    return ret;
}

int ksu_patch_text(void *dst, void *src, size_t len, int flags)
{
    struct patch_text_info info = {
        .dst = dst,
        .src = src,
        .len = len,
        .cpu_count = ATOMIC_INIT(0),
        .flags = flags,
    };
    return stop_machine(ksu_patch_text_cb, &info, cpu_online_mask);
}

/* Scan [start, start+size) for a BL whose target equals `target`; returns the first match, or NULL. */
void *scan_call_to(void *start, size_t size, void *target)
{
    const uint32_t *insn = (const uint32_t *)start;
    size_t count = size / sizeof(uint32_t);
    size_t i;

    for (i = 0; i < count; i++) {
        int32_t imm26;
        void *branch_target;

        if ((insn[i] & 0xFC000000U) != 0x94000000U)
            continue;

        imm26 = (int32_t)((insn[i] & 0x03FFFFFFU) << 6) >> 6;
        branch_target = (void *)((uintptr_t)(&insn[i]) + ((int64_t)imm26 << 2));

        if (branch_target == target)
            return (void *)&insn[i];
    }
    return NULL;
}

#endif /* __aarch64__ */
