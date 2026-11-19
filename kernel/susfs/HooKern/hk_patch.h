// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef LKMHOOK_HK_PATCH_H
#define LKMHOOK_HK_PATCH_H

#include <linux/list.h>
#include <linux/types.h>

#define HK_PATCH_INSNS_MAX 32

/*
 * writing kernel text. the library owns the mechanisms and nothing else: which
 * one a call takes is decided at the call site, and whether a write is verified
 * before it lands is decided there too. there is no mode word and no process
 * wide setting, so one look at a call tells the whole story
 *
 * every primitive writes len bytes at dst, cleans the data cache over the range
 * and invalidates the instruction cache, and leaves no mapping behind on any
 * path, including the failing ones. the return value is 0 or a negative errno
 *
 * hk_write_kernel is the kernel's own aarch64_insn_patch_text: it parks the cores
 * and maps through the kernel's own fixmap frame, so it assumes nothing about the
 * headers this module was built with. hk_write_fixmap maps a slot of this build
 * itself, which is the byte wide path and the faster one.
 * hk_write_direct stores where the kernel already mapped the page writable, and
 * hk_va_writable is the test that says whether that is the case
 *
 * a caller that wants a fallback, a retry, or a permanent switch of path writes
 * that in its own wrapper. the library keeps no such state
 */
int hk_write_text(void *dst, const void *src, size_t len);
int hk_write_kernel(void *dst, const void *src, size_t len);
int hk_write_one(void *dst, u32 insn);
/*
 * what one write is made of. every field is the caller's choice: a translation of
 * its own or one of the library's, the library's slot or the kernel's, a plain
 * store or a fault tolerant one, and whether the result has to be read back. a
 * zeroed descriptor is the library's own route and the read back is off
 */
#define HK_SLOT_LIBRARY 0
#define HK_SLOT_KERNEL 1
#define HK_SLOT_OWN 2

struct hk_write {
	struct mm_struct *mm;
	unsigned long (*to_pa)(unsigned long va);
	void (*flush)(struct mm_struct *mm, unsigned long va);
	pte_t (*mkwrite)(pte_t pte);
	/*
	 * the same freedom the alias routes give through to_pa, for the route that writes
	 * through the target's own entry: it has to find that entry first, and a caller
	 * that wants to find it its own way says so here. a null one is the library's own
	 * walk, and the library neither checks nor second guesses what it is given
	 */
	pte_t *(*pte_of)(struct mm_struct *mm, unsigned long va, int *level);
	int slot;
	bool nofault;
	bool verify;
};

int hk_write_run(void *dst, const void *src, size_t len,
		 const struct hk_write *w);
int hk_write_poke(void *dst, const void *src, size_t len);
/*
 * the write bits of a mapping held open for a caller that stores its own way. a null
 * fn means the store is done here through the same descriptor, so a caller with a
 * foreign address space runs its own store inside and a caller with one of its own
 * takes the store here
 */
int hk_write_window(void *dst, const void *src, size_t len,
		    const struct hk_write *w,
		    int (*fn)(void *ctx, void *addr, size_t len), void *ctx);
int hk_write_fixmap(void *dst, const void *src, size_t len);
int hk_write_fixmap_by(void *dst, const void *src, size_t len,
		       unsigned long (*to_pa)(unsigned long va));
int hk_write_direct(void *dst, const void *src, size_t len);

/*
 * what a caller needs to judge a destination itself. the frame test the verified
 * slot path runs is the same one hk_va_maps answers
 */
unsigned long hk_va_to_pa(unsigned long va);
unsigned long hk_va_walk_pa(unsigned long va);
/*
 * which family an address belongs to. the family is what decides how a write has to
 * be made, so a caller that wants to choose its own route asks this first instead of
 * guessing from the value. the library never uses it to pick a route by itself
 */
enum hk_va_kind {
	HK_VA_IMAGE_TEXT = 0,	/* the kernel's own patcher translates this arithmetically */
	HK_VA_IMAGE_DATA,	/* image rodata and data: its patcher sends these to vmalloc_to_page */
	HK_VA_LINEAR,		/* the linear map: read only data has a read only alias here too */
	HK_VA_VMALLOC,		/* vmalloc and module memory */
	HK_VA_USER,
};

int hk_va_kind(unsigned long va);
int hk_va_level(unsigned long va);
bool hk_va_writable(unsigned long va);
/*
 * one address, every fact a caller needs to choose a route itself. this is the
 * queries above together, for a layer that wants to decide once instead of asking
 * four times: the library never picks a route from it. the protection a vendor
 * kernel puts on a page shows up as writable being false while the address still
 * translates, and a block mapping as level above zero, which is the shape a page
 * table write cannot take
 */
struct hk_route {
	int kind;		/* enum hk_va_kind of the address */
	int level;		/* walk level, above zero means a block mapping */
	bool block_map;		/* no pte to change, text writes still work */
	bool writable;		/* writable through the current page table */
	bool maps;		/* the entry carries the frame the address translates to */
};

/*
 * fill one of these in. nothing is written, nothing is installed: it reads the page
 * table and answers what it found. a protection this library has no name for shows
 * up as a false in the record, which is the honest answer to a caller that wanted
 * to know whether its own route stands a chance. the breakpoint gates are a
 * question of their own, hk_kprobe_check_at, so a caller that wants both asks both
 */
int hk_route_probe(unsigned long addr, struct hk_route *out);
bool hk_va_maps(unsigned long va, unsigned long pa);
bool hk_va_maps_by(unsigned long va, unsigned long pa,
		   unsigned long (*to_pa)(unsigned long va));

void hk_flush_icache(unsigned long addr);
void hk_flush_dcache(unsigned long addr, size_t len);

/*
 * one patch site a transaction covers. the raw bytes read at prepare time are
 * kept and checksummed, the commit refuses to write when the bytes at the
 * address changed since, so a site another writer already took is reported
 * instead of being overwritten
 */
struct hk_patch_hook {
	struct list_head list;
	const char *name;
	void *dst;
	const void *src;
	size_t len;
	int flags;
	u8 orig[HK_PATCH_INSNS_MAX * 4];
	u32 checksum;
	bool prepared;
	bool active;
};

struct hk_patch_set {
	struct list_head hooks;
	struct list_head list;
	unsigned int count;
};

/*
 * the transaction writes through the path hk_init was configured with, one word
 * at a time, and a hook carries no mode of its own
 */
void hk_patch_set_init(struct hk_patch_set *set);
void hk_patch_set_release(struct hk_patch_set *set);
int hk_patch_prepare(struct hk_patch_set *set, struct hk_patch_hook *hook);
int hk_patch_commit(struct hk_patch_set *set);
int hk_patch_rollback(struct hk_patch_set *set);
unsigned int hk_patch_set_count(const struct hk_patch_set *set);

/*
 * the configured path for one unsigned long, which is what a table entry needs
 */
int hk_patch_write(void *dst, unsigned long val);

/*
 * called by hk_init, not by consumers: resolve what the primitives need, in
 * process context, and record the configured write path
 */
/*
 * what a consumer compiles in is its own build decision. setting the flag leaves
 * this module out of the link, and the calls the core makes into it become
 * nothing, while the entry points of the module itself stay declared: calling one
 * of those without its object is a link error, never a silent no op
 */
#ifdef HK_NO_PATCH
static inline void hk_patch_init(void) { }
static inline void hk_patch_set_write(int (*write)(void *dst, const void *src,
						   size_t len))
{
	(void)write;
}
#else
void hk_patch_init(void);
bool hk_patch_kernel_primitive_ok(unsigned long addr);
void hk_patch_set_write(int (*write)(void *dst, const void *src,
				    size_t len));
#endif

#endif
