// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/crc32.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/cpu.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/stop_machine.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <asm/cacheflush.h>
#include <asm/fixmap.h>
#include <asm/memory.h>
#include <asm/pgtable.h>

#include "hk.h"
#include "hk_patch.h"

/*
 * the base is the first slot of the early ioremap window and not FIX_TEXT_POKE0.
 * the enum between the two is not spare: a kernel with UNMAP_KERNEL_AT_EL0 adds
 * FIX_ENTRY_TRAMP_TEXT1 to TEXT4 above FIX_TEXT_POKE0, the KPTI entry
 * trampoline is mapped there by map_entry_trampoline, and a patch that opened
 * one of those slots and unmapped it again would clear a live trampoline page
 * table entry. FIX_BTMAP_END is __end_of_permanent_fixed_addresses in every
 * build of this series, so it sits above the whole permanent window including
 * the trampoline slots, and the window it starts is the boot only early ioremap
 * one, which nothing holds after early_ioremap_reset
 */
#define HK_FIXMAP_SLOT_BASE FIX_BTMAP_END
#define HK_FIXMAP_SLOT_MAX 8

typedef void (*clean_inval_fn)(unsigned long start, unsigned long end);
typedef void (*fixmap_fn)(unsigned long idx, phys_addr_t phys, pgprot_t prot);
typedef int (*insn_write_fn)(void *addr, u32 insn);
typedef int (*insn_patch_fn)(void *addr[], u32 insn[], int count);

/*
 * one mutex covers the whole path because the paths sleep: __set_fixmap flushes
 * a TLB, stop_machine parks the other cores and aarch64_insn_patch_text takes
 * cpus_read_lock. a spinlock cannot be held over any of them, so it is not held
 * at all and every caller is documented as process context
 */
static DEFINE_MUTEX(g_patch_lock);
/*
 * the path a plain call takes, set once by hk_init from the configuration of the
 * consumer and never changed afterwards. the library keeps no other state, and
 * every decision about fallbacks, retries and tests belongs to the caller
 */
static int (*g_write)(void *dst, const void *src, size_t len);
typedef struct vm_struct *(*find_vm_area_fn)(const void *addr);
#define HK_WALK_PTE 0
#define HK_WALK_PMD 1
#define HK_WALK_PUD 2
#define HK_WALK_P4D 3

static unsigned long *hk_dst_pte(unsigned long addr);
/*
 * the range the kernel translates arithmetically when it patches: is_image_text is
 * core_kernel_text, which is text and nothing else. rodata and data are not in it
 * even though they are part of the image, and the kernel sends those to
 * vmalloc_to_page, which is the whole reason this question exists
 */
static unsigned long g_stext;
static unsigned long g_etext;
static bool g_stext_read;

static bool hk_core_text(unsigned long addr)
{
	unsigned long fn;

	if (!g_stext_read) {
		fn = hk_resolve("_stext");
		if (fn && hk_ker_addr_ok(fn))
			g_stext = fn;
		fn = hk_resolve("_etext");
		if (fn && hk_ker_addr_ok(fn))
			g_etext = fn;
		g_stext_read = true;
	}
	if (!g_stext || !g_etext)
		return false;
	return addr >= g_stext && addr < g_etext;
}

static bool hk_kernel_image_addr(unsigned long addr);
static __nocfi noinline int hk_translate(unsigned long addr, unsigned long *out);

static int hk_walk_pa(unsigned long addr, unsigned long *out,
		     unsigned long *desc, int *level);

static int hk_slot_write(void *dst, const void *src, size_t len,
			unsigned long (*to_pa)(unsigned long va), bool nofault);

/*
 * an address the kernel's own patch_map would hand to vmalloc_to_page is every
 * address that is not core kernel text, which is the same test the kernel makes
 */
static find_vm_area_fn g_find_vm_area;

/*
 * a resolved kernel symbol cannot be called from a checked site on the old cfi
 * builds, the type clang recorded for it is not the one the cast produces, so the
 * call lives behind a wrapper whose body is off, the same shape every other
 * resolved call in this file has
 */
static __nocfi noinline struct vm_struct *call_find_vm_area(const void *addr)
{
	if (!g_find_vm_area)
		return NULL;
	return g_find_vm_area(addr);
}
typedef int (*nofault_write_fn)(void *dst, const void *src, size_t len);
static nofault_write_fn g_nofault_write;

/*
 * a store that cannot land has to come back as an error, and the helper that does
 * it is resolved at run time like everything else here: the kernel does not export
 * it, so naming it at link time would leave the module with a symbol the running
 * kernel may not have
 */
static __nocfi noinline int call_nofault_write(void *dst, const void *src,
					       size_t len)
{
	if (!g_nofault_write)
		return -EOPNOTSUPP;
	return g_nofault_write(dst, src, len);
}
static unsigned long (*g_vmalloc_to_pfn_fn)(const void *addr);
static unsigned long g_kimage_voffset;
static bool g_kimage_voffset_read;


/*
 * the target's own entry decides whether the alias is needed at all. a page the
 * kernel already maps writable is written in place, exactly like patch_map does
 * when the strict rwx configs are off
 */
/*
 * the kernel's own translation, which is what the kernel's own patcher uses and
 * therefore agrees with it byte for byte. a kernel older than 5.13 answers a block
 * mapped address with a null page instead of an error, and the frame that comes
 * out of it is whatever the arithmetic makes of that, so the answer is checked
 * against the memory the kernel owns before it is handed back
 */
unsigned long hk_va_to_pa(unsigned long va)
{
	unsigned long pa = 0;

	if (hk_translate(va, &pa))
		return 0;
	if (!pfn_valid(pa >> PAGE_SHIFT))
		return 0;
	return pa;
}

static clean_inval_fn g_clean_inval;
static fixmap_fn g_set_fixmap;
static insn_write_fn g_insn_write;
static insn_patch_fn g_insn_patch;
static unsigned long g_image_start;
static unsigned long g_image_end;
static bool g_image_read;

void hk_patch_init(void)
{
	unsigned long fn;
	int missing = 0;

	fn = hk_resolve("__set_fixmap");
	if (fn && hk_ker_addr_ok(fn))
		g_set_fixmap = (fixmap_fn)fn;
	else
		missing++;

	fn = hk_resolve("caches_clean_inval_pou");
	if (!fn)
		fn = hk_resolve("dcache_clean_inval_poc");
	if (!fn)
		fn = hk_resolve("__flush_icache_range");
	if (fn && hk_ker_addr_ok(fn))
		g_clean_inval = (clean_inval_fn)fn;
	else
		missing++;

	fn = hk_resolve("aarch64_insn_write");
	if (fn && hk_ker_addr_ok(fn))
		g_insn_write = (insn_write_fn)fn;
	else
		missing++;

	fn = hk_resolve("aarch64_insn_patch_text");
	if (fn && hk_ker_addr_ok(fn))
		g_insn_patch = (insn_patch_fn)fn;
	else
		missing++;

	/*
	 * the two the slot path needs are optional on their own: the image and
	 * the linear map are translated arithmetically and never ask for them
	 */

	/*
	 * the two the block mapping test needs. core_kernel_text is the kernel's own
	 * test for the addresses its patcher translates arithmetically, and
	 * vmalloc_to_page is asked directly for the block mapped ones
	 */
	fn = hk_resolve("copy_to_kernel_nofault");
	if (fn && hk_ker_addr_ok(fn))
		g_nofault_write = (nofault_write_fn)fn;

	pr_info("[lkmhook] patch symbols ready, %d optional missing\n", missing);
}

/*
 * the index is an enum constant plus 0 to 7, so it folds at compile time and the
 * bound is checked then too: an index over FIX_BTMAP_BEGIN belongs to the page
 * table fixmaps at the end of the enum, which are live, and the check refuses it
 * before __set_fixmap, whose own guard is a BUG_ON. the dst warning in the write
 * path is the runtime half of the same answer, it probes the address the slot
 * actually resolved to
 */
static int hk_patch_slot_index(int *out)
{
	unsigned long idx = HK_FIXMAP_SLOT_BASE;
	if (idx > FIX_BTMAP_BEGIN) {
		pr_warn("[lkmhook] fixmap base 0x%lx lands outside the early ioremap window\n",
			idx);
		return -EINVAL;
	}
	*out = (int)idx;
	return 0;
}

static __nocfi noinline int call_clean_inval(unsigned long start,
					     unsigned long end)
{
	unsigned long fn;

	if (!g_clean_inval) {
		fn = hk_resolve("caches_clean_inval_pou");
		if (!fn)
			fn = hk_resolve("dcache_clean_inval_poc");
		if (!fn)
			fn = hk_resolve("__flush_icache_range");
		if (!fn) {
			pr_warn("[lkmhook] no cache clean fn, the write is refused\n");
			return -ENOENT;
		}
		g_clean_inval = (clean_inval_fn)fn;
	}
	g_clean_inval(start, end);
	return 0;
}

static __nocfi noinline int call_set_fixmap(unsigned long idx,
					    phys_addr_t phys, pgprot_t prot)
{
	if (!g_set_fixmap) {
		unsigned long fn = hk_resolve("__set_fixmap");

		if (!fn) {
			pr_warn("[lkmhook] __set_fixmap not found, the write is refused\n");
			return -ENOENT;
		}
		g_set_fixmap = (fixmap_fn)fn;
	}
	g_set_fixmap(idx, phys, prot);
	return 0;
}

/*
 * the copy goes through the alias __set_fixmap just opened, the only way into
 * read only text, and it is a plain memcpy that faults with no way back. the gate
 * therefore runs before every store and a destination that is not mapped is
 * refused with -ENXIO instead of being written. it is a page table walk and not
 * a probe: a probe reports the same thing but only after the walk has already
 * answered, and a walk cannot be fooled by a slot that maps a page of zeros
 *
 * the walk is the one the kernel itself uses on init_mm, so it stays correct for
 * whatever paging levels the running kernel was built with. a block mapping at
 * any level means there is no pte to read and the gate refuses
 */
static unsigned long *hk_dst_pte(unsigned long addr)
{
	struct mm_struct *mm;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	mm = (struct mm_struct *)hk_resolve("init_mm");
	if (!mm || !hk_ker_addr_ok((unsigned long)mm))
		return NULL;
	pgd = pgd_offset(mm, addr);
	if (pgd_none(*pgd) || pgd_bad(*pgd))
		return NULL;
	p4d = p4d_offset(pgd, addr);
	if (p4d_none(*p4d) || p4d_bad(*p4d))
		return NULL;
	pud = pud_offset(p4d, addr);
	if (pud_none(*pud) || pud_bad(*pud))
		return NULL;
	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd) || pmd_bad(*pmd))
		return NULL;
	return (unsigned long *)pte_offset_kernel(pmd, addr);
}

/*
 * the library's own translation. every level is asked for none, bad and leaf in
 * that order, because on arm64 a block descriptor fails the table test, so the
 * leaf question has to be asked first. the kernel's own vmalloc_to_page only
 * learned that in 5.13, and its 5.10 form warns and returns NULL for a block,
 * which is a wrong frame rather than an error
 */
static int hk_walk_pa(unsigned long addr, unsigned long *out,
		     unsigned long *desc, int *level)
{
	struct mm_struct *mm;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	/* a non kernel address has no entry in init_mm, walk nothing */
	if (!hk_ker_addr_ok(addr))
		return -EINVAL;
	mm = (struct mm_struct *)hk_resolve("init_mm");
	if (!mm || !hk_ker_addr_ok((unsigned long)mm))
		return -ENOENT;

	pgd = pgd_offset(mm, addr);
	if (pgd_none(*pgd) || pgd_bad(*pgd))
		return -ENOENT;
	p4d = p4d_offset(pgd, addr);
	if (p4d_none(*p4d) || p4d_bad(*p4d))
		return -ENOENT;
#ifdef p4d_leaf
	if (p4d_leaf(*p4d)) {
		*out = __p4d_to_phys(*p4d) + (addr & ~P4D_MASK);
		if (desc)
			*desc = p4d_val(*p4d);
		if (level)
			*level = HK_WALK_P4D;
		return 0;
	}
#endif
	pud = pud_offset(p4d, addr);
	if (pud_none(*pud) || pud_bad(*pud))
		return -ENOENT;
#ifdef pud_leaf
	if (pud_leaf(*pud)) {
		*out = __pud_to_phys(*pud) + (addr & ~PUD_MASK);
		if (desc)
			*desc = pud_val(*pud);
		if (level)
			*level = HK_WALK_PUD;
		return 0;
	}
#endif
	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd))
		return -ENOENT;
#ifdef pmd_leaf
	if (pmd_leaf(*pmd)) {
		*out = __pmd_to_phys(*pmd) + (addr & ~PMD_MASK);
		if (desc)
			*desc = pmd_val(*pmd);
		if (level)
			*level = HK_WALK_PMD;
		return 0;
	}
#endif
	if (pmd_bad(*pmd))
		return -ENOENT;
	pte = pte_offset_kernel(pmd, addr);
	if (!pte_present(*pte))
		return -ENOENT;
	*out = __pte_to_phys(*pte) + (addr & ~PAGE_MASK);
	if (desc)
		*desc = pte_val(*pte);
	if (level)
		*level = HK_WALK_PTE;
	return 0;
}

unsigned long hk_va_walk_pa(unsigned long va)
{
	unsigned long pa = 0;

	if (hk_walk_pa(va, &pa, NULL, NULL))
		return 0;
	return pa;
}

/*
 * the same walk, answering what a caller checks a destination with. the answer
 * comes from the descriptor of the level that maps the address, so a block mapping
 * answers as truthfully as a page one
 */
int hk_route_probe(unsigned long addr, struct hk_route *out)
{
	unsigned long pa;

	if (!out || !addr)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	out->kind = hk_va_kind(addr);
	out->level = hk_va_level(addr);
	out->block_map = out->level > 0;
	out->writable = hk_va_writable(addr);
	pa = hk_va_walk_pa(addr);
	out->maps = pa && hk_va_maps(addr, pa);
	return 0;
}

bool hk_va_writable(unsigned long va)
{
	unsigned long pa = 0;
	unsigned long desc = 0;

	if (hk_walk_pa(va, &pa, &desc, NULL))
		return false;
	return pte_write(__pte(desc));
}

bool hk_va_maps(unsigned long va, unsigned long pa)
{
	unsigned long got = 0;

	if (hk_walk_pa(va, &got, NULL, NULL))
		return false;
	return (got & PAGE_MASK) == (pa & PAGE_MASK);
}

/*
 * image addresses are the one range virt_addr_valid cannot answer for: the image
 * sits in the vmalloc window, so the linear mapping test fails on it while __pa
 * would subtract the wrong offset. _text and _end are the bounds and both are
 * read once. the answer is a plain range test, a bare virtual address inside the
 * image is never handed to vmalloc_to_pfn, whose pmd walk reads a block mapping
 * as if it were a pte table and returns a physical address from unrelated page
 * contents. a kernel without the two symbols answers false and the vmalloc
 * branch is gated by find_vm_area instead
 */
static bool hk_kernel_image_addr(unsigned long addr)
{
	unsigned long start;
	unsigned long end;

	if (!g_image_read) {
		start = hk_resolve("_text");
		if (!start)
			start = hk_resolve("_stext");
		end = hk_resolve("_end");
		if (!end)
			end = hk_resolve("__bss_stop");
		g_image_read = true;
		if (!start || !end || end <= start ||
		    !hk_ker_addr_ok(start) || !hk_ker_addr_ok(end)) {
			pr_warn_once("[lkmhook] _text/_end unavailable, image addresses fall to the vmalloc branch\n");
			return false;
		}
		g_image_start = start;
		g_image_end = end;
	}
	return g_image_start && addr >= g_image_start && addr < g_image_end;
}

static __nocfi noinline int hk_translate(unsigned long addr, unsigned long *out)
{
	unsigned long fn;
	unsigned long phys;
	unsigned long pfn;
	unsigned long voff;
	bool image;

	/*
	 * the order matters. the image range is asked first because it is the one
	 * test that needs a symbol, and a caller that names an address no path can
	 * reach gets -EIO with no walk attempted, so a runaway value cannot reach
	 * vmalloc_to_pfn and come back with a physical address built from whatever
	 * the pmd entry it read happened to hold
	 */
	image = hk_kernel_image_addr(addr);
	if (!image && !__is_lm_address(addr) && !is_vmalloc_addr((void *)addr)) {
		pr_warn("[lkmhook] 0x%lx is not a kernel address this build can reach\n",
			addr);
		return -EIO;
	}

	if (image) {
		if (!g_kimage_voffset_read) {
			fn = hk_resolve("kimage_voffset");
			if (!fn || !hk_ker_addr_ok(fn)) {
				pr_warn("[lkmhook] kimage_voffset not found, image addresses cannot be translated\n");
				return -ENOENT;
			}
			if (copy_from_kernel_nofault(&voff, (void *)fn,
						     sizeof(voff))) {
				pr_warn("[lkmhook] kimage_voffset unreadable\n");
				return -EFAULT;
			}
			g_kimage_voffset = voff;
			g_kimage_voffset_read = true;
		}
		if (!g_kimage_voffset) {
			pr_warn("[lkmhook] kimage_voffset is zero\n");
			return -EINVAL;
		}
		*out = addr - g_kimage_voffset;
		return 0;
	}

	if (__is_lm_address(addr)) {
		*out = __pa(addr);
		return 0;
	}

	if (!g_find_vm_area) {
		fn = hk_resolve("find_vm_area");
		if (fn && hk_ker_addr_ok(fn))
			g_find_vm_area = (find_vm_area_fn)fn;
	}
	if (g_find_vm_area && !g_find_vm_area((const void *)addr)) {
		pr_warn("[lkmhook] 0x%lx is not a registered vmalloc area\n",
			addr);
		return -EIO;
	}
	if (!g_vmalloc_to_pfn_fn) {
		fn = hk_resolve("vmalloc_to_pfn");
		if (!fn || !hk_ker_addr_ok(fn)) {
			pr_warn("[lkmhook] vmalloc_to_pfn not found, the write is refused\n");
			return -ENOENT;
		}
		g_vmalloc_to_pfn_fn = (unsigned long (*)(const void *))fn;
	}
	pfn = g_vmalloc_to_pfn_fn((const void *)addr);
	if (!pfn) {
		pr_warn("[lkmhook] 0x%lx has no page\n", addr);
		return -EFAULT;
	}
	phys = (pfn << PAGE_SHIFT) + (addr & ~PAGE_MASK);
	*out = phys;
	return 0;
}

struct hk_patch_job {
	unsigned long addr;
	const void *src;
	size_t len;
	int flags;
	int idx;
	unsigned long caller;
	unsigned long (*to_pa)(unsigned long va);
	bool nofault;
	bool kernel_slot;
	atomic_t arrived;
	int ret;
	/*
	 * released by the master once the slot is closed and the writes are done,
	 * the other cpus spin on it before they return from the callback
	 */
	int done;
};

/*
 * the write through the alias. one slot serves any length, the alias is opened
 * per physical page and dropped again. the caller holds g_patch_lock, so the
 * slot is not shared and neither is the kernel patch slot
 */
static __nocfi noinline int hk_patch_slot_run(unsigned long addr, const void *src, size_t len,
			     int flags, int idx, unsigned long caller,
			     unsigned long (*to_pa)(unsigned long va),
			     bool nofault)
{
	size_t left = len;
	int ret = 0;

	while (left) {
		unsigned long phys;
		unsigned long dst;
		size_t chunk;
		bool opened = false;

		phys = to_pa(addr);
		if (!phys) {
			ret = -ENOENT;
			break;
		}
		chunk = min(left, PAGE_SIZE - (phys & ~PAGE_MASK));

		ret = call_set_fixmap(idx, phys & PAGE_MASK, PAGE_KERNEL);
		if (ret)
			break;
		dst = __fix_to_virt(idx) + (phys & ~PAGE_MASK);
		if (ret) {
			call_set_fixmap(idx, 0, __pgprot(0));
			break;
		}
		opened = true;

		if (nofault) {
			ret = (int)call_nofault_write((void *)dst, src, chunk);
		} else {
			memcpy((void *)dst, src, chunk);
			dsb(ish);
		}
		if (opened)
			call_set_fixmap(idx, 0, __pgprot(0));
		if (ret)
			break;

		ret = call_clean_inval(addr, addr + chunk);
		if (ret)
			break;
		hk_flush_icache(addr);

		src += chunk;
		addr += chunk;
		left -= chunk;
	}
	return ret;
}

/*
 * one fixmap slot and one page table entry are shared by the whole machine, so
 * only one of the stopped cpus may open it. the last one to arrive is the master,
 * the others spin until it is done, which is what the kernel's own patcher does
 * for the same reason. without this every stopped cpu opens and closes the slot
 * under the others and a cpu that passed the destination gate can lose the entry
 * before its store lands
 */
static int hk_patch_slot_cb(void *data)
{
	struct hk_patch_job *job = data;

	if (atomic_inc_return(&job->arrived) != num_online_cpus()) {
		while (!smp_load_acquire(&job->done))
			cpu_relax();
		return 0;
	}
	job->ret = hk_patch_slot_run(job->addr, job->src, job->len, job->flags,
				     job->idx, job->caller, job->to_pa,
				     job->nofault);
	smp_store_release(&job->done, 1);
	return job->ret;
}

typedef struct module *(*hk_module_address_fn)(unsigned long addr);

/*
 * the module a text or data address belongs to, NULL for anything else. the
 * symbol is resolved at runtime. it is asked before the slot path is entered
 * because a module address has a page table the kernel's own patch primitive
 * already knows how to reach, while the frame the slot path computes is a build
 * constant of this module and does not have to be the one the running kernel
 * mapped its fixmap window with
 */
static bool hk_is_module_addr(unsigned long addr)
{
	static hk_module_address_fn fn;
	static bool tried;

	if (!tried) {
		unsigned long sym = hk_resolve("__module_address");

		tried = true;
		if (sym && hk_ker_addr_ok(sym))
			fn = (hk_module_address_fn)sym;
	}
	return fn && fn(addr) != NULL;
}

/*
 * the kernel's own patch primitive, stop_machine inside. stop_machine_cpuslocked
 * asserts that cpus_read_lock is held, kprobe and ftrace call it from under
 * theirs, so it is taken here. the symbol is resolved at runtime because it is
 * not exported
 */
static __nocfi int hk_insn_patch_once(unsigned long addr, const void *src,
				     size_t len)
{
	void *addrs[HK_PATCH_INSNS_MAX];
	u32 insns[HK_PATCH_INSNS_MAX];
	const u32 *in = src;
	int count = (int)(len / 4);
	int ret;
	int i;

	if ((len & 3) || (addr & 3)) {
		pr_warn("[lkmhook] insn patch wants a word aligned range\n");
		return -EINVAL;
	}
	if (count < 1 || count > HK_PATCH_INSNS_MAX) {
		pr_warn("[lkmhook] insn patch takes 1 to %d instructions\n",
			HK_PATCH_INSNS_MAX);
		return -E2BIG;
	}


	for (i = 0; i < count; i++) {
		addrs[i] = (void *)(addr + i * 4);
		insns[i] = in[i];
	}
	if (!g_insn_patch) {
		unsigned long fn = hk_resolve("aarch64_insn_patch_text");

		if (!fn) {
			pr_warn("[lkmhook] aarch64_insn_patch_text not found\n");
			return -ENOENT;
		}
		g_insn_patch = (insn_patch_fn)fn;
	}

	cpus_read_lock();
	ret = g_insn_patch(addrs, insns, count);
	cpus_read_unlock();
	if (ret)
		return ret;
	return call_clean_inval(addr, addr + len);
}

/*
 * the primitive takes a table of addresses and is bounded by HK_PATCH_INSNS_MAX,
 * which is what its own callers use, so a longer run is split into calls of that
 * size. the whole run is still covered by cpus_read_lock around each call and by
 * the caller's g_patch_lock across all of them
 */
static int hk_insn_patch_run(unsigned long addr, const void *src, size_t len)
{
	size_t chunk;
	int ret;

	while (len) {
		chunk = min_t(size_t, len, HK_PATCH_INSNS_MAX * 4);
		ret = hk_insn_patch_once(addr, src, chunk);
		if (ret)
			return ret;
		addr += chunk;
		src += chunk;
		len -= chunk;
	}
	return 0;
}

/*
 * the kernel's own slot, one instruction at a time, caller flushes. the address
 * has to be readable as a vmalloc or image address for the kernel's own
 * translation, so it is checked here before the call
 */
static __nocfi int hk_insn_write_run(unsigned long addr, const void *src,
				     size_t len)
{
	const u32 *in = src;
	unsigned long start = addr;
	unsigned long left = len;
	unsigned long phys;
	int ret;

	if ((len & 3) || (addr & 3) || ((addr & ~PAGE_MASK) + len) > PAGE_SIZE) {
		pr_warn("[lkmhook] insn write wants one aligned page of words\n");
		return -EINVAL;
	}
	ret = hk_translate(addr, &phys);
	if (ret)
		return ret;
	if (!g_insn_write) {
		unsigned long fn = hk_resolve("aarch64_insn_write");

		if (!fn) {
			pr_warn("[lkmhook] aarch64_insn_write not found\n");
			return -ENOENT;
		}
		g_insn_write = (insn_write_fn)fn;
	}
	cpus_read_lock();
	while (left) {
		ret = g_insn_write((void *)addr, *in);
		if (ret)
			break;
		addr += 4;
		in++;
		left -= 4;
	}
	cpus_read_unlock();
	if (ret)
		return ret;
	ret = call_clean_inval(start, start + len);
	if (ret)
		return ret;
	hk_flush_icache(start);
	return 0;
}

/*
 * the slot path, shared by the verified and the raw entry points. the alias is
 * opened per physical page and dropped again, and the caller holds g_patch_lock,
 * so the slot is not shared and neither is the kernel patch slot. one slot serves
 * any length: up to four bytes are written where the caller stands, longer runs
 * park the cores first, which is what the kernel's own patcher does
 */
/*
 * the slot path, with the translation the caller named. a null one means the
 * library's own walk, which is the only one that is correct on a kernel whose
 * vmalloc_to_page predates leaf handling, so it is the default of hk_write_fixmap
 */
/*
 * the caller's translation, or the library's walk when it named none
 */
static unsigned long (*hk_write_to_pa(const struct hk_write *w))(unsigned long)
{
	if (w && w->to_pa)
		return w->to_pa;
	return hk_va_walk_pa;
}

/*
 * the kernel's own poke slot, the same one its patcher maps through, so a platform
 * that watches fixmap use sees what it expects. the target's own page is left
 * alone: this is a mapping made here and it is writable whatever the target says
 */
static int hk_poke_run(struct hk_patch_job *job, const struct hk_write *w)
{
	unsigned long addr = job->addr;
	const void *src = job->src;
	size_t left = job->len;
	int ret = 0;

	lockdep_assert_held(&g_patch_lock);
	cpus_read_lock();
	while (left) {
		unsigned long phys = hk_write_to_pa(w)(addr);
		size_t chunk;
		void *map;

		if (!phys) {
			ret = -ENOENT;
			break;
		}
		chunk = min(left, PAGE_SIZE - (phys & ~PAGE_MASK));
		ret = call_set_fixmap(FIX_TEXT_POKE0, phys & PAGE_MASK,
				      PAGE_KERNEL);
		if (ret)
			break;
		map = (void *)(__fix_to_virt(FIX_TEXT_POKE0) + (phys & ~PAGE_MASK));
		if (w->nofault)
			ret = (int)call_nofault_write(map, src, chunk);
		else {
			memcpy(map, src, chunk);
			dsb(ish);
		}
		if (!ret)
			ret = call_clean_inval(addr, addr + chunk);
		call_set_fixmap(FIX_TEXT_POKE0, 0, __pgprot(0));
		if (ret)
			break;
		addr += chunk;
		src += chunk;
		left -= chunk;
	}
	cpus_read_unlock();
	return ret;
}

/*
 * the page table entry that maps an address, and the level it was mapped at. a
 * block mapping has no entry of its own, so the caller is told instead of being
 * handed a descriptor of a different shape
 */
static pte_t *hk_va_pte(struct mm_struct *mm, unsigned long addr, int *level)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	pgd = pgd_offset(mm, addr);
	if (pgd_none(*pgd) || pgd_bad(*pgd))
		return NULL;
	p4d = p4d_offset(pgd, addr);
	if (p4d_none(*p4d) || p4d_bad(*p4d))
		return NULL;
#ifdef p4d_leaf
	if (p4d_leaf(*p4d)) {
		*level = HK_WALK_P4D;
		return NULL;
	}
#endif
	pud = pud_offset(p4d, addr);
	if (pud_none(*pud) || pud_bad(*pud))
		return NULL;
#ifdef pud_leaf
	if (pud_leaf(*pud)) {
		*level = HK_WALK_PUD;
		return NULL;
	}
#endif
	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd))
		return NULL;
#ifdef pmd_leaf
	if (pmd_leaf(*pmd)) {
		*level = HK_WALK_PMD;
		return NULL;
	}
#endif
	if (pmd_bad(*pmd))
		return NULL;
	*level = HK_WALK_PTE;
	return pte_offset_kernel(pmd, addr);
}

/*
 * the write bits of an entry, and only those: the read only bit is cleared and the
 * software write bit is set, so what the store dirties is kept and the entry can be
 * put back by clearing the write bits again. a caller whose kernel needs something
 * else here names its own through the descriptor
 */
static pte_t hk_pte_make_write(const struct hk_write *w, pte_t pte)
{
	pteval_t bits;

	if (w->mkwrite)
		return w->mkwrite(pte);
	bits = pte_val(pte);
	bits &= ~PTE_RDONLY;
	bits |= PTE_WRITE;
	return __pte(bits);
}

/*
 * the kernel side invalidation. an address space of the caller's own is its
 * business, so its flush comes in through the descriptor
 */
static void hk_flush_tlb_page(unsigned long addr);

static void hk_flush_va(const struct hk_write *w, struct mm_struct *mm,
			unsigned long addr)
{
	if (w->flush)
		w->flush(mm, addr);
	else
		hk_flush_tlb_page(addr);
}

static void hk_flush_tlb_page(unsigned long addr)
{
	__flush_tlb_kernel_pgtable(addr);
	dsb(ish);
	isb();
}

/*
 * the target's own entry is made writable for the store and only that bit is put
 * back, so the dirty and young bits the store sets are kept. a platform that
 * watches fixmap use sees nothing here, which is what this route is for
 */
static __nocfi noinline int hk_own_run(void *dst, const void *src, size_t len,
		      const struct hk_write *w,
		      int (*fn)(void *ctx, void *addr, size_t len), void *ctx)
{
	struct mm_struct *mm = w->mm;
	unsigned long addr = (unsigned long)dst;
	int ret = 0;

	if (!mm) {
		mm = (struct mm_struct *)hk_resolve("init_mm");
		if (!mm || !hk_ker_addr_ok((unsigned long)mm))
			return -ENOENT;
	}

	/*
	 * a caller that stores its own way gets one window over the page it named,
	 * which is what a foreign address space needs, and the store is the whole of
	 * it. the write bits are the only ones that move, so what the store dirties
	 * is kept when the entry goes back
	 */
	while (len) {
		unsigned long off = addr & ~PAGE_MASK;
		size_t chunk = min(len, PAGE_SIZE - off);
		int level = HK_WALK_PTE;
		pte_t *ptep;

		ptep = w->pte_of ? w->pte_of(mm, addr, &level) :
				   hk_va_pte(mm, addr, &level);
		pte_t old;
		bool was_ro;

		if (!ptep) {
			ret = level == HK_WALK_PTE ? -ENOENT : -EOPNOTSUPP;
			break;
		}
		old = READ_ONCE(*ptep);
		was_ro = !pte_write(old);
		WRITE_ONCE(*ptep, hk_pte_make_write(w, pte_mkdirty(old)));
		hk_flush_va(w, mm, addr);
		if (fn)
			ret = fn(ctx, (void *)addr, chunk);
		else if (w->nofault)
			ret = call_nofault_write((void *)addr, src, chunk);
		else {
			memcpy((void *)addr, src, chunk);
			dsb(ish);
		}
		if (was_ro)
			WRITE_ONCE(*ptep, pte_wrprotect(READ_ONCE(*ptep)));
		hk_flush_va(w, mm, addr);
		if (!ret)
			ret = call_clean_inval(addr, addr + chunk);
		if (ret)
			break;
		addr += chunk;
		src += chunk;
		len -= chunk;
	}
	return ret;
}

int hk_write_window(void *dst, const void *src, size_t len,
		    const struct hk_write *w,
		    int (*fn)(void *ctx, void *addr, size_t len), void *ctx)
{
	struct hk_write o = { .slot = HK_SLOT_OWN };
	int ret;

	if (!dst || (!src && !fn) || (!len && !fn))
		return -EINVAL;
	if (w)
		o = *w;
	if (o.slot != HK_SLOT_OWN) {
		if (fn)
			return -EOPNOTSUPP;
		return hk_write_run(dst, src, len, &o);
	}
	mutex_lock(&g_patch_lock);
	ret = hk_own_run(dst, src, len, &o, fn, ctx);
	mutex_unlock(&g_patch_lock);
	if (!ret && !fn && o.verify && memcmp(dst, src, len))
		ret = -EIO;
	return ret;
}

int __nocfi noinline hk_write_run(void *dst, const void *src, size_t len,
		 const struct hk_write *w)
{
	struct hk_write o = { };
	int ret;

	if (!dst || !src || !len)
		return -EINVAL;
	if (w)
		o = *w;
	if (o.slot == HK_SLOT_OWN) {
		mutex_lock(&g_patch_lock);
		ret = hk_own_run(dst, src, len, &o, NULL, NULL);
		mutex_unlock(&g_patch_lock);
	} else if (o.slot == HK_SLOT_KERNEL) {
		struct hk_patch_job job = {
			.addr = (unsigned long)dst,
			.src = src,
			.len = len,
			.to_pa = hk_write_to_pa(&o),
			.nofault = o.nofault,
		};

		mutex_lock(&g_patch_lock);
		ret = hk_poke_run(&job, &o);
		mutex_unlock(&g_patch_lock);
	} else {
		ret = hk_slot_write(dst, src, len, hk_write_to_pa(&o),
				    o.nofault);
	}
	if (!ret && o.verify && memcmp(dst, src, len))
		ret = -EIO;
	return ret;
}

int hk_write_poke(void *dst, const void *src, size_t len)
{
	const struct hk_write w = { .slot = HK_SLOT_KERNEL,
				    .nofault = true, .verify = true };

	return hk_write_run(dst, src, len, &w);
}

static int hk_slot_write(void *dst, const void *src, size_t len,
			unsigned long (*to_pa)(unsigned long va), bool nofault)
{
	struct hk_patch_job job;
	unsigned long addr = (unsigned long)dst;
	unsigned long caller = (unsigned long)__builtin_return_address(0);
	int ret;

	if (!dst || !src || !len || (len & 3))
		return -EINVAL;

	mutex_lock(&g_patch_lock);
	ret = hk_patch_slot_index(&job.idx);
	if (ret)
		goto out;

	job.addr = addr;
	job.src = src;
	job.len = len;
	job.flags = 0;
	job.caller = caller;
	job.to_pa = to_pa ? to_pa : hk_va_walk_pa;
	job.nofault = nofault;
	atomic_set(&job.arrived, 0);
	job.done = 0;
	job.ret = 0;

	if (len <= 4) {
		ret = hk_patch_slot_run(addr, src, len, 0, job.idx, caller,
					job.to_pa, nofault);
	} else {
		struct hk_patch_job saved = job;

		ret = stop_machine(hk_patch_slot_cb, &job, cpu_online_mask);
		if (ret)
			pr_warn("[lkmhook] patch 0x%lx+%zu mode fixmap failed %d\n",
				saved.addr, saved.len, ret);
	}
out:
	mutex_unlock(&g_patch_lock);
	return ret;
}

int hk_write_fixmap_by(void *dst, const void *src, size_t len,
		       unsigned long (*to_pa)(unsigned long va))
{
	return hk_slot_write(dst, src, len, to_pa, false);
}

int hk_write_fixmap(void *dst, const void *src, size_t len)
{
	return hk_write_fixmap_by(dst, src, len, NULL);
}

/*
 * false when the kernel's own patcher would send this address to a translation
 * that cannot answer it. the engine asks this before it lets a consumer configured
 * primitive touch its own memory
 */
bool hk_patch_kernel_primitive_ok(unsigned long addr)
{
	unsigned long phys = 0;
	int level = HK_WALK_PTE;

	/*
	 * the kernel translates its own text arithmetically and never through
	 * vmalloc_to_page, so text is safe whatever the mapping looks like
	 */
	if (hk_core_text(addr))
		return true;
	if (hk_walk_pa(addr, &phys, NULL, &level))
		return true;
	return level == HK_WALK_PTE;
}

/*
 * the level an address is mapped at, for a caller that wants to judge a
 * destination itself: 0 page, 1 pmd block, 2 pud block, 3 p4d block, -1 unknown
 */
int hk_va_kind(unsigned long va)
{
	unsigned long phys = 0;

	if (hk_core_text(va))
		return HK_VA_IMAGE_TEXT;
	if (hk_kernel_image_addr(va))
		return HK_VA_IMAGE_DATA;
	if (!va)
		return HK_VA_USER;
	if (call_find_vm_area((const void *)va))
		return HK_VA_VMALLOC;
	if (hk_walk_pa(va, &phys, NULL, NULL) == 0)
		return HK_VA_LINEAR;
	if (va < TASK_SIZE)
		return HK_VA_USER;
	return HK_VA_LINEAR;
}

int hk_va_level(unsigned long va)
{
	unsigned long phys = 0;
	int level = HK_WALK_PTE;

	if (hk_walk_pa(va, &phys, NULL, &level))
		return -1;
	return level;
}

int hk_write_kernel(void *dst, const void *src, size_t len)
{
	unsigned long addr = (unsigned long)dst;

	if (!dst || !src || !len || (len & 3))
		return -EINVAL;
	/* a store outside the kernel mapping has no page to land on */
	if (!hk_ker_addr_ok(addr))
		return -EIO;
	/*
	 * the kernel sends everything that is not its own text to vmalloc_to_page,
	 * and that cannot answer a block mapped address before 5.13: it warns and
	 * returns a null page, which the patcher turns into BUG_ON. those addresses
	 * go through the library's own walk instead, so no caller has to know and
	 * nothing warns
	 */
	if (!hk_patch_kernel_primitive_ok(addr))
		return hk_write_fixmap(dst, src, len);
	return hk_insn_patch_run(addr, src, len);
}

int hk_write_one(void *dst, u32 insn)
{
	if (!dst)
		return -EINVAL;
	return hk_insn_write_run((unsigned long)dst, &insn, sizeof(insn));
}

/*
 * the page is written where it is. the caller checked it with hk_va_writable,
 * the library does not repeat the test
 */
int hk_write_direct(void *dst, const void *src, size_t len)
{
	if (!dst || !src || !len)
		return -EINVAL;
	memcpy(dst, (const void *)src, len);
	dsb(ish);
	return call_clean_inval((unsigned long)dst, (unsigned long)dst + len);
}

int __nocfi hk_write_text(void *dst, const void *src, size_t len)
{
	if (g_write)
		return g_write(dst, src, len);
	return hk_write_kernel(dst, src, len);
}

/*
 * called once by hk_init, in process context, with the write path the consumer
 * configured. the library only records it
 */
void hk_patch_set_write(int (*write)(void *dst, const void *src, size_t len))
{
	g_write = write;
}

int __nocfi hk_patch_write(void *dst, unsigned long val)
{
	return hk_write_text(dst, &val, sizeof(val));
}

void hk_patch_set_init(struct hk_patch_set *set)
{
	if (!set)
		return;
	INIT_LIST_HEAD(&set->hooks);
	INIT_LIST_HEAD(&set->list);
	set->count = 0;
}

unsigned int hk_patch_set_count(const struct hk_patch_set *set)
{
	return set ? set->count : 0;
}

/*
 * the precheck. nothing is written, the current bytes are read through the
 * nofault helper and kept with their checksum, and every reason an install could
 * fail is answered here so the commit that follows has none left
 */
int hk_patch_prepare(struct hk_patch_set *set, struct hk_patch_hook *hook)
{
	unsigned long phys;
	int ret;

	if (!set || !hook || !hook->dst || !hook->src || !hook->len)
		return -EINVAL;
	if (hook->prepared || hook->active)
		return -EALREADY;
	if (hook->len > sizeof(hook->orig) || (hook->len & 3))
		return -EINVAL;

	ret = hk_translate((unsigned long)hook->dst, &phys);
	if (ret) {
		pr_warn("[lkmhook] prepare %s: 0x%lx does not translate, %d\n",
			hook->name ? hook->name : "?",
			(unsigned long)hook->dst, ret);
		return ret;
	}
	if (copy_from_kernel_nofault(hook->orig, hook->dst, hook->len)) {
		pr_warn("[lkmhook] prepare %s: site is not readable\n",
			hook->name ? hook->name : "?");
		return -EFAULT;
	}
	hook->checksum = crc32_le(~0U, hook->orig, hook->len);
	hook->prepared = true;
	hook->active = false;
	if (!hook->list.next) {
		list_add_tail(&hook->list, &set->hooks);
		set->count++;
	}
	return 0;
}

/*
 * the commit. every site is compared against the bytes the prepare saw before
 * the first write, so a site another writer took is reported while nothing has
 * changed. a write that fails after that point puts the sites already written
 * back and reports the failure, so a set is either all in or all out
 */
int __nocfi hk_patch_commit(struct hk_patch_set *set)
{
	struct hk_patch_hook *hook;
	struct hk_patch_hook *failed = NULL;
	u8 seen[HK_PATCH_INSNS_MAX * 4];
	int ret = 0;

	if (!set)
		return -EINVAL;

	list_for_each_entry(hook, &set->hooks, list) {
		if (!hook->prepared) {
			pr_warn("[lkmhook] commit %s: not prepared\n",
				hook->name ? hook->name : "?");
			return -EINVAL;
		}
		if (copy_from_kernel_nofault(seen, hook->dst, hook->len)) {
			pr_warn("[lkmhook] commit %s: site is not readable\n",
				hook->name ? hook->name : "?");
			return -EFAULT;
		}
		if (crc32_le(~0U, seen, hook->len) != hook->checksum) {
			pr_warn("[lkmhook] commit %s: site changed since prepare\n",
				hook->name ? hook->name : "?");
			return -EAGAIN;
		}
	}

	list_for_each_entry(hook, &set->hooks, list) {
		ret = hk_write_text(hook->dst, hook->src, hook->len);
		if (ret) {
			failed = hook;
			break;
		}
		hook->active = true;
	}
	if (!ret)
		return 0;

	list_for_each_entry(hook, &set->hooks, list) {
		if (hook == failed)
			break;
		hk_write_text(hook->dst, hook->orig, hook->len);
		hook->active = false;
	}
	pr_warn("[lkmhook] commit rolled back at %s, %d\n",
		failed && failed->name ? failed->name : "?", ret);
	return ret;
}

int __nocfi hk_patch_rollback(struct hk_patch_set *set)
{
	struct hk_patch_hook *hook;
	int first = 0;

	if (!set)
		return -EINVAL;

	list_for_each_entry(hook, &set->hooks, list) {
		int ret;

		if (!hook->active)
			continue;
		ret = hk_write_text(hook->dst, hook->orig, hook->len);
		if (ret) {
			pr_warn("[lkmhook] rollback %s failed %d\n",
				hook->name ? hook->name : "?", ret);
			if (!first)
				first = ret;
			continue;
		}
		hook->active = false;
	}
	return first;
}

void hk_patch_set_release(struct hk_patch_set *set)
{
	struct hk_patch_hook *hook;
	struct hk_patch_hook *tmp;

	if (!set)
		return;
	list_for_each_entry_safe(hook, tmp, &set->hooks, list) {
		list_del(&hook->list);
		hook->prepared = false;
		set->count--;
	}
	INIT_LIST_HEAD(&set->hooks);
	set->count = 0;
}
