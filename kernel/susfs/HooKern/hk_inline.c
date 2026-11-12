// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/errno.h>
#include <linux/list.h>
#include <asm/tlbflush.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/rcupdate_trace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/vmalloc.h>

#include "hk.h"
#include "hk_patch.h"
#include "hk_inline.h"

#define HK_TRAMP_SIZE 4096
#define HK_INS_LDR_X17 0x58000051
#define HK_INS_LDR_X17_P12 0x58000071
#define HK_INS_B_X12 0x14000003
#define HK_INS_BR_X17 0xD61F0220
#define HK_INS_RET_X17 0xD65F0220
#define HK_INS_BLR_X17 0xD63F0220
#define HK_INS_NOP 0xD503201F
#define HK_INS_BTI_JC 0xD50324DF
#define HK_INS_BR_ANY 0xD61F0000
#define HK_INS_BR_MASK 0xFFFFFC1F
#define HK_INS_BRK_ANY 0xD4200000
#define HK_INS_BRK_MASK 0xFFE00000
#define HK_INS_MOV_MASK 0x7F800000
#define HK_INS_MOVZ 0x52800000
#define HK_INS_MOVK 0x72800000
#define HK_INS_MOV_X9_LR 0xAA1E03E9

/* bit 50 of an arm64 pte, the BTI guarded attribute of the page */
#define HK_PTE_GP 0x0004000000000000UL

#define HK_BTI_GUARDED(_pte) ((_pte) & HK_PTE_GP)

/* dry run sink for the probe, the tables allow 8 words per instruction */
#define HK_PROBE_SCRATCH 128

/* serializes every install and restore, see the note at the lock site */
static DEFINE_MUTEX(g_inline_lock);

/*
 * one node per live hook window, under g_inline_lock. the window belongs to the
 * install and is handed back by hk_inline_disable or hk_inline_free, so a second
 * hook can take the address once the first entry is restored. the hook pointer
 * is the name of the node, a symbol the caller keeps alive while it is hooked
 */
struct hk_inline_own {
	struct list_head list;
	struct hk_inline *hook;
	unsigned long start;
	u32 len;
};

static LIST_HEAD(g_inline_owned);

/*
 * an unload that could not finish. the trampoline is still reachable from the
 * entry, so it must not be freed, and rmmod must not be allowed to unmap it. the
 * node is the only thing the exit gate needs to retry
 */
struct hk_inline_pending {
	struct list_head list;
	struct hk_inline *hook;
};

static LIST_HEAD(g_inline_pending);

typedef enum {
	HK_INST_B = 1,
	HK_INST_BC,
	HK_INST_BL,
	HK_INST_ADR,
	HK_INST_ADRP,
	HK_INST_LDR_32,
	HK_INST_LDR_64,
	HK_INST_LDRSW,
	HK_INST_PRFM,
	HK_INST_LDR_SIMD_32,
	HK_INST_LDR_SIMD_64,
	HK_INST_LDR_SIMD_128,
	HK_INST_CBZ,
	HK_INST_CBNZ,
	HK_INST_TBZ,
	HK_INST_TBNZ,
	HK_INST_IGNORE,
} hk_inst_type_t;

static const u32 hk_masks[] = {
	0xFC000000, 0xFF000010, 0xFC000000, 0x9F000000, 0x9F000000,
	0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000,
	0xFF000000, 0xFF000000, 0xFF000000,
	0x7F000000, 0x7F000000, 0x7F000000, 0x7F000000,
	0x00000000,
};

static const u32 hk_types[] = {
	0x14000000, 0x54000000, 0x94000000, 0x10000000, 0x90000000,
	0x18000000, 0x58000000, 0x98000000, 0xD8000000,
	0x1C000000, 0x5C000000, 0x9C000000,
	0x34000000, 0x35000000, 0x36000000, 0x37000000,
	0x00000000,
};

/*
 * words the relocation of one instruction emits. it has to match the emitters
 * below exactly, hk_relo_in_tramp walks these numbers to turn an address inside
 * the patched window into the address the same instruction has in the trampoline
 */
static const int hk_relo_len[] = {
	6, 8, 6, 4, 4,
	5, 5, 5, 7, 7, 7, 7,
	8, 8, 8, 8,
	2,
};

/*
 * the three tables are indexed by the same instruction class, and the compiler
 * can hold that: a class added to the enum without a mask, a type pattern and a
 * length is a build error and not a wrong trampoline at runtime
 */
static_assert(ARRAY_SIZE(hk_masks) == ARRAY_SIZE(hk_types),
	      "the class masks and the class patterns disagree");
static_assert(ARRAY_SIZE(hk_relo_len) == ARRAY_SIZE(hk_masks),
	      "the relocation lengths and the instruction classes disagree");

static __nocfi u32 hk_get_insn(const u8 *p)
{
	return le32_to_cpu(*(const u32 *)p);
}

static __nocfi long hk_sext(u64 v, int bits)
{
	if (bits < 64 && (v & (1ULL << (bits - 1))))
		v |= ~0ULL << bits;
	return (long)v;
}

static __nocfi u32 hk_enc_movz(u32 rd, u32 imm, u32 hw)
{
	return 0xD2800000 | (hw << 21) | ((imm & 0xFFFF) << 5) | rd;
}

static __nocfi u32 hk_enc_movk(u32 rd, u32 imm, u32 hw)
{
	return 0xF2800000 | (hw << 21) | ((imm & 0xFFFF) << 5) | rd;
}

static bool hk_is_b(u32 insn)
{
	return (insn & hk_masks[0]) == hk_types[0];
}

static bool hk_is_hint(u32 insn)
{
	return (insn & 0xFFFFFC1F) == 0xD503201F;
}

static u64 hk_decode_b_target(u32 insn, u64 pc)
{
	return pc + hk_sext(insn & 0x03FFFFFF, 26) * 4;
}

/* a resolver result can be stale and a b target is arithmetic, so neither the
 * probe nor the branch walk dereferences one directly */
static __nocfi int hk_read_code(unsigned long addr, void *dst, size_t len)
{
	return copy_from_kernel_nofault(dst, (const void *)addr, len);
}

static __nocfi int hk_fetch_insn(unsigned long addr, u32 *out)
{
	u8 raw[4];

	if (hk_read_code(addr, raw, sizeof(raw)))
		return -EFAULT;
	*out = hk_get_insn(raw);
	return 0;
}

/*
 * the entry a hook patches: a symbolic entry is often a thunk of b or of a hint
 * followed by b, and patching the thunk leaves the real body alone. the walk
 * reads through the nofault helper, a chain that cannot be read stops the walk
 * and every hook that lands on an unreadable target is refused later
 */
static __nocfi int hk_resolve_branch_chain(unsigned long addr,
					   unsigned long *out)
{
	u32 insn;
	u32 next;
	int depth;

	for (depth = 0; depth < 32; depth++) {
		if (hk_fetch_insn(addr, &insn))
			return -EFAULT;
		if (hk_is_b(insn)) {
			addr = hk_decode_b_target(insn, addr);
			continue;
		}
		if (!hk_is_hint(insn))
			break;
		if (hk_fetch_insn(addr + 4, &next))
			return -EFAULT;
		if (!hk_is_b(next))
			break;
		addr = hk_decode_b_target(next, addr + 4);
	}
	*out = addr;
	return 0;
}

struct hk_relo_ctx {
	u32 *dst;
	u32 count;
	const u8 *src;		/* entry window copy, NULL reads tramp_start */
	unsigned long inst_addr;
	unsigned long tramp_start;
	unsigned long tramp_end;
	unsigned long backup_start;
};

static __nocfi hk_inst_type_t hk_insn_type(u32 insn)
{
	int i;

	for (i = 0; i < (int)ARRAY_SIZE(hk_masks); i++)
		if ((insn & hk_masks[i]) == hk_types[i])
			return (hk_inst_type_t)(i + 1);
	return HK_INST_IGNORE;
}

/* the tables are 0 based, the instruction types are not */
static int hk_insn_index(u32 insn)
{
	return (int)hk_insn_type(insn) - 1;
}

static bool hk_in_tramp(const struct hk_relo_ctx *c, u64 addr)
{
	return addr >= c->tramp_start && addr < c->tramp_end;
}

/*
 * an operand that points back into the window has to point at the copy of that
 * instruction in the trampoline after relocation, and the copy sits at the sum
 * of the lengths of everything emitted before it
 */
static u64 hk_relo_in_tramp(const struct hk_relo_ctx *c, u64 addr)
{
	const u8 *src = c->src ? c->src : (const u8 *)c->tramp_start;
	u64 fix = c->backup_start;
	u32 idx;
	u32 inst;
	u32 j;

	if (!hk_in_tramp(c, addr))
		return addr;
	idx = (addr - c->tramp_start) / 4;
	if (idx >= HK_INLINE_ENTRY_MAX)
		return addr;
	for (j = 0; j < idx; j++) {
		inst = hk_get_insn(src + j * 4);
		fix += hk_relo_len[hk_insn_index(inst)] * 4;
	}
	return fix;
}

/*
 * ldr x17, #8 ; b #12 ; lo ; hi ; br x17 ; nop
 * the literal sits behind the branch so the sequence is one straight line and
 * four words is the shortest form, the nop keeps every emission the same length
 */
static __nocfi int hk_relo_stub(struct hk_relo_ctx *c, u64 addr, bool wrap)
{
	c->dst[c->count++] = HK_INS_LDR_X17;
	c->dst[c->count++] = 0x14000003;
	c->dst[c->count++] = addr & 0xFFFFFFFF;
	c->dst[c->count++] = addr >> 32;
	if (wrap)
		c->dst[c->count++] = HK_INS_BR_X17;
	else
		c->dst[c->count++] = HK_INS_RET_X17;
	c->dst[c->count++] = HK_INS_NOP;
	return 0;
}

static __nocfi int hk_relo_abs_jump(struct hk_relo_ctx *c, u64 target)
{
	c->dst[c->count++] = HK_INS_LDR_X17;
	c->dst[c->count++] = HK_INS_RET_X17;
	c->dst[c->count++] = target & 0xFFFFFFFF;
	c->dst[c->count++] = target >> 32;
	return 0;
}

static __nocfi int hk_relo_b(struct hk_relo_ctx *c, u32 insn,
			     hk_inst_type_t type)
{
	u64 disp;
	u64 addr;

	if (type == HK_INST_BC)
		disp = hk_sext((insn >> 5) & 0x7FFFF, 19) << 2;
	else
		disp = hk_sext(insn & 0x03FFFFFF, 26) << 2;
	addr = c->inst_addr + disp;
	addr = hk_relo_in_tramp(c, addr);

	/*
	 * a conditional branch has to keep its own two instruction test and land
	 * on the stub that follows it, so it is rewritten to skip four words
	 * instead of three
	 */
	if (type == HK_INST_BC) {
		c->dst[c->count++] = (insn & 0xFF00001F) | 0x40;
		c->dst[c->count++] = 0x14000006;
	}
	return hk_relo_stub(c, addr, type != HK_INST_BL);
}

/*
 * the immediate an adr or adrp carries, split across two fields: the high one has
 * to be shifted into place before the low one joins it
 */
static __nocfi long hk_adr_imm(u32 insn)
{
	return hk_sext((((insn >> 5) & 0x7FFFF) << 2) | ((insn >> 29) & 0x3), 21);
}

static __nocfi int hk_relo_bl(struct hk_relo_ctx *c, u32 insn)
{
	u64 addr = c->inst_addr + hk_sext(insn & 0x03FFFFFF, 26) * 4;

	addr = hk_relo_in_tramp(c, addr);
	/*
	 * the call returns to the word after blr, so a branch has to sit there and
	 * carry it over the literal, or the return lands inside it
	 */
	c->dst[c->count++] = HK_INS_LDR_X17_P12;
	c->dst[c->count++] = HK_INS_BLR_X17;
	c->dst[c->count++] = HK_INS_B_X12;
	c->dst[c->count++] = addr & 0xFFFFFFFF;
	c->dst[c->count++] = addr >> 32;
	c->dst[c->count++] = HK_INS_NOP;
	return 0;
}

static __nocfi int hk_relo_adr(struct hk_relo_ctx *c, u32 insn,
			       hk_inst_type_t type)
{
	u32 xd = insn & 0x1F;
	u64 addr;

	if (type == HK_INST_ADR) {
		addr = c->inst_addr + hk_adr_imm(insn);
	} else {
		/*
		 * the immediate of adrp counts pages, so the byte offset is that
		 * count times the page size, not the count itself
		 */
		addr = (c->inst_addr & ~0xFFFUL) +
		       (u64)(hk_adr_imm(insn) * 4096);
		if (hk_in_tramp(c, addr))
			return -EOPNOTSUPP;
	}
	c->dst[c->count++] = 0x58000040 | xd;
	c->dst[c->count++] = HK_INS_B_X12;
	c->dst[c->count++] = addr & 0xFFFFFFFF;
	c->dst[c->count++] = addr >> 32;
	return 0;
}

static __nocfi int hk_relo_ldr(struct hk_relo_ctx *c, u32 insn,
			       hk_inst_type_t type)
{
	u32 rt = insn & 0x1F;
	u64 addr = c->inst_addr + hk_sext((insn >> 5) & 0x7FFFF, 19) * 4;

	if (hk_in_tramp(c, addr) && type != HK_INST_PRFM)
		return -EOPNOTSUPP;
	addr = hk_relo_in_tramp(c, addr);

	if (type == HK_INST_LDR_32 || type == HK_INST_LDR_64 ||
	    type == HK_INST_LDRSW) {
		u32 op;

		if (type == HK_INST_LDR_32)
			op = 0xB9400000;
		else if (type == HK_INST_LDR_64)
			op = 0xF9400000;
		else
			op = 0xB9800000;
		c->dst[c->count++] = 0x58000060 | rt;
		c->dst[c->count++] = op | rt | (rt << 5);
		c->dst[c->count++] = 0x14000003;
		c->dst[c->count++] = addr & 0xFFFFFFFF;
		c->dst[c->count++] = addr >> 32;
	} else {
		u32 op;

		/*
		 * the simd and prefetch literals are read into a register the
		 * original instruction does not name, so x16 and x17 are saved
		 * around the load and restored after it
		 */
		if (type == HK_INST_PRFM)
			op = 0xF9800220;
		else if (type == HK_INST_LDR_SIMD_32)
			op = 0xBD400220;
		else if (type == HK_INST_LDR_SIMD_64)
			op = 0xFD400220;
		else
			op = 0x3DC00220;
		c->dst[c->count++] = 0xA93F47F0;
		c->dst[c->count++] = 0x58000091;
		c->dst[c->count++] = op | rt;
		c->dst[c->count++] = 0xA8C147F0;
		c->dst[c->count++] = 0x14000003;
		c->dst[c->count++] = addr & 0xFFFFFFFF;
		c->dst[c->count++] = addr >> 32;
	}
	return 0;
}

static __nocfi int hk_relo_cb(struct hk_relo_ctx *c, u32 insn)
{
	u64 addr = c->inst_addr + hk_sext((insn >> 5) & 0x7FFFF, 19) * 4;

	addr = hk_relo_in_tramp(c, addr);
	c->dst[c->count++] = (insn & 0xFF00001F) | 0x40;
	c->dst[c->count++] = 0x14000006;
	return hk_relo_stub(c, addr, true);
}

static __nocfi int hk_relo_tb(struct hk_relo_ctx *c, u32 insn)
{
	u64 addr = c->inst_addr + hk_sext((insn >> 5) & 0x3FFF, 14) * 4;

	addr = hk_relo_in_tramp(c, addr);
	c->dst[c->count++] = (insn & 0xFFF8001F) | 0x40;
	c->dst[c->count++] = 0x14000006;
	return hk_relo_stub(c, addr, true);
}

static __nocfi int hk_relo_inst(struct hk_relo_ctx *c, u32 insn)
{
	int ret;

	switch (hk_insn_type(insn)) {
	case HK_INST_B:
	case HK_INST_BC:
		ret = hk_relo_b(c, insn, hk_insn_type(insn));
		break;
	case HK_INST_BL:
		ret = hk_relo_bl(c, insn);
		break;
	case HK_INST_ADR:
	case HK_INST_ADRP:
		ret = hk_relo_adr(c, insn, hk_insn_type(insn));
		break;
	case HK_INST_LDR_32:
	case HK_INST_LDR_64:
	case HK_INST_LDRSW:
	case HK_INST_PRFM:
	case HK_INST_LDR_SIMD_32:
	case HK_INST_LDR_SIMD_64:
	case HK_INST_LDR_SIMD_128:
		ret = hk_relo_ldr(c, insn, hk_insn_type(insn));
		break;
	case HK_INST_CBZ:
	case HK_INST_CBNZ:
		ret = hk_relo_cb(c, insn);
		break;
	case HK_INST_TBZ:
	case HK_INST_TBNZ:
		ret = hk_relo_tb(c, insn);
		break;
	default:
		c->dst[c->count++] = insn;
		c->dst[c->count++] = HK_INS_NOP;
		ret = 0;
		break;
	}
	return ret;
}

/* one class, one swing at the emitters: the api note is in the header */
int hk_relo_check(void)
{
	u32 dst[HK_INLINE_ENTRY_MAX * 8];
	u32 window[HK_INLINE_ENTRY_MAX];
	unsigned long base = (unsigned long)&hk_relo_check;
	struct hk_relo_ctx c;
	char detail[64];
	u32 i;
	u32 j;
	int ret;

	for (i = 0; i < ARRAY_SIZE(hk_relo_len); i++) {
		if (hk_insn_index(hk_types[i]) != (int)i) {
			scnprintf(detail, sizeof(detail),
				  "class %u matches class %d first", i,
				  hk_insn_index(hk_types[i]));
			hk_notice(HK_NOTICE_RELO_TABLE, detail);
			return -EIO;
		}
		memset(&c, 0, sizeof(c));
		memset(window, 0, sizeof(window));
		window[0] = hk_types[i];
		for (j = 1; j < HK_INLINE_ENTRY_MAX; j++)
			window[j] = HK_INS_NOP;
		c.dst = dst;
		c.src = (const u8 *)window;
		c.inst_addr = base;
		c.tramp_start = base + 0x1000;
		c.tramp_end = base + 0x1000 + HK_INLINE_PATCH_LEN;
		c.backup_start = base + 0x2000;
		memset(dst, 0, sizeof(dst));
		ret = hk_relo_inst(&c, window[0]);
		if (ret)
			return ret;
		if (c.count != (u32)hk_relo_len[i]) {
			scnprintf(detail, sizeof(detail),
				  "class %u emitted %u words, the table says %d",
				  i, c.count, hk_relo_len[i]);
			hk_notice(HK_NOTICE_RELO_TABLE, detail);
			return -EIO;
		}
	}
	return 0;
}

typedef void *(*hk_vmalloc_node_range_fn)(unsigned long size,
					  unsigned long align,
					  unsigned long start,
					  unsigned long end,
					  gfp_t gfp_mask,
					  pgprot_t prot,
					  unsigned long vm_flags,
					  int node,
					  const void *caller);

static __nocfi noinline void *hk_exec_alloc(unsigned long size)
{
	hk_vmalloc_node_range_fn fn;
	const char *name = "__vmalloc_node_range";

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
	name = "__vmalloc_node_range_noprof";
#endif
	fn = (hk_vmalloc_node_range_fn)hk_resolve(name);
	if (!fn || !hk_ker_addr_ok((unsigned long)fn))
		return NULL;
	return fn(size, 1, VMALLOC_START, VMALLOC_END,
		  GFP_KERNEL | __GFP_NOWARN, PAGE_KERNEL_EXEC,
		  VM_FLUSH_RESET_PERMS, NUMA_NO_NODE,
		  __builtin_return_address(0));
}

static __nocfi noinline void hk_exec_free(void *mem)
{
	vfree(mem);
}

/* the entry address a hook on sym patches: the resolver result with the same
 * branch chain walked, so the collision window and the probe agree */
static __nocfi int hk_read_addr(const char *sym, unsigned long *out)
{
	unsigned long addr;
	int ret;

	addr = hk_resolve(sym);
	if (!addr)
		return -ENOENT;
	ret = hk_resolve_branch_chain(addr, &addr);
	if (ret)
		return ret;
	if (!hk_ker_addr_ok(addr))
		return -EINVAL;
	*out = addr;
	return 0;
}

/* the window of another live hook that overlaps [start, start + len), NULL when
 * the range is free. the caller holds g_inline_lock */
static const struct hk_inline_own *hk_inline_overlap(unsigned long start,
						     u32 len)
{
	const struct hk_inline_own *ent;
	unsigned long end = start + len;

	list_for_each_entry(ent, &g_inline_owned, list) {
		if (start < ent->start + ent->len && ent->start < end)
			return ent;
	}
	return NULL;
}

/* name both sides of the overlap, the caller holds g_inline_lock */
static void hk_inline_report(const char *what, const char *sym,
			     unsigned long start, u32 len,
			     const struct hk_inline_own *other)
{
	pr_warn("[lkmhook] %s %s window 0x%lx+%u collides with %s 0x%lx+%u\n",
		what, sym, start, len,
		other->hook->name ? other->hook->name : "?",
		other->start, other->len);
}

/*
 * claim [start, start + len) for h, -EBUSY when another live hook owns part of
 * it. a claim is taken before any text is read or written and a failed install
 * hands it back, so every claim has an owner that can release it
 */
/*
 * the engine writes its own trampoline and jump stubs, which live in module memory,
 * and it writes the target text. a module address is vmalloc, so on a kernel whose
 * vmalloc_to_page predates leaf handling the kernel primitive cannot reach it, and
 * the configured path is therefore only taken where the kernel can take it
 */
static int hk_engine_write(void *dst, const void *src, size_t len)
{
	if (!hk_patch_kernel_primitive_ok((unsigned long)dst))
		return hk_write_fixmap(dst, src, len);
	return hk_write_text(dst, src, len);
}

static int hk_inline_own(struct hk_inline *h, const char *sym,
			 unsigned long start, u32 len)
{
	const struct hk_inline_own *other;
	struct hk_inline_own *own;
	struct hk_inline_own *ent;

	mutex_lock(&g_inline_lock);
	list_for_each_entry(ent, &g_inline_owned, list) {
		if (ent->hook != h)
			continue;
		pr_warn("[lkmhook] inline %s already installed\n", sym);
		mutex_unlock(&g_inline_lock);
		return -EBUSY;
	}
	other = hk_inline_overlap(start, len);
	if (other) {
		hk_inline_report("inline", sym, start, len, other);
		mutex_unlock(&g_inline_lock);
		return -EBUSY;
	}

	own = kzalloc(sizeof(*own), GFP_KERNEL);
	if (!own) {
		mutex_unlock(&g_inline_lock);
		return -ENOMEM;
	}
	own->hook = h;
	own->start = start;
	own->len = len;
	list_add(&own->list, &g_inline_owned);
	mutex_unlock(&g_inline_lock);
	return 0;
}

/* hand the window back so a later install can take it */
static void hk_inline_release(const struct hk_inline *h)
{
	struct hk_inline_own *ent;

	mutex_lock(&g_inline_lock);
	list_for_each_entry(ent, &g_inline_owned, list) {
		if (ent->hook != h)
			continue;
		list_del(&ent->list);
		mutex_unlock(&g_inline_lock);
		kfree(ent);
		return;
	}
	mutex_unlock(&g_inline_lock);
}

/* the probe half of the claim: name the live window that would refuse an install
 * on [start, start + len), 0 when the range is free */
static int hk_inline_collide(const char *sym, unsigned long start, u32 len,
			     unsigned long *out)
{
	const struct hk_inline_own *other;

	mutex_lock(&g_inline_lock);
	other = hk_inline_overlap(start, len);
	if (!other) {
		mutex_unlock(&g_inline_lock);
		return 0;
	}
	*out = other->start;
	hk_inline_report("probe", sym, start, len, other);
	mutex_unlock(&g_inline_lock);
	return -EBUSY;
}

/*
 * the page table entry of a kernel virtual address. the walk is the generic one
 * and it is handed the kernel's own init_mm, resolved at runtime because the
 * symbol is not in the module namespace of every build. a block mapping at any
 * level means the page has no pte this code could change, which is a refused
 * guard rather than a written entry
 */
static unsigned long *hk_pte_of(unsigned long addr)
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
 * an entry shorter than the window: a ret, a br or an unconditional branch ends the
 * function, so the instructions after it belong to whatever the linker put next.
 * copying those into a trampoline and jumping back into the middle of them is what
 * a short function cannot survive, so the window is judged before anything is
 * written and the caller is told to take the breakpoint route instead
 */
static int hk_inline_short(const u32 *win, unsigned int words)
{
	unsigned int i;

	for (i = 0; i < words; i++) {
		bool ret = (win[i] & 0xFFFFFC1F) == 0xD65F0000;
		bool br = (win[i] & 0xFFFFFC1F) == 0xD61F0000;
		bool b = (win[i] & 0xFC000000) == 0x14000000;

		if ((ret || br || b) && i + 1 < words)
			return -EOPNOTSUPP;
	}
	return 0;
}


/*
 * the continuation of every relocated branch is an indirect branch, and an
 * indirect branch into a page whose pte carries the guarded attribute traps
 * unless the first instruction is a bti landing pad. the resumption points are
 * the instructions after the patched window, which are not landing pads, so the
 * attribute is cleared for exactly the pages that hold one and recorded for the
 * free path to put back. the attribute is not cleared on the entry page, a
 * caller that reaches the entry indirectly needs the landing pad there
 */
static int hk_bti_guard_track(struct hk_inline *h, unsigned long addr)
{
	struct hk_inline_guard *guard;
	unsigned long page = addr & PAGE_MASK;
	unsigned long *ptep;
	unsigned long pte;
	u32 i;

	for (i = 0; i < h->guard_count; i++) {
		if (h->guard[i].page == page)
			return 0;
	}
	if (h->guard_count >= HK_INLINE_GUARD_MAX)
		return -ENOSPC;
	ptep = hk_pte_of(page);
	if (!ptep)
		return -EFAULT;
	pte = READ_ONCE(*ptep);
	if (!HK_BTI_GUARDED(pte))
		return 0;

	guard = &h->guard[h->guard_count++];
	guard->page = page;
	guard->ptep = ptep;
	guard->pte = pte;
	guard->had_gp = true;
	guard->active = true;
	/*
	 * the guarded bit is part of the translation, so the entry alone is not
	 * enough: without the invalidation a cpu keeps the old translation and the
	 * branch back into the middle of the function still trips BTI
	 */
	WRITE_ONCE(*ptep, pte & ~HK_PTE_GP);
	__flush_tlb_kernel_pgtable(page);
	dsb(ish);
	isb();
	if (READ_ONCE(*ptep) & HK_PTE_GP) {
		/* the bit cannot be taken away here, so the hook must not be set up */
		WRITE_ONCE(*ptep, pte);
		__flush_tlb_kernel_pgtable(page);
		dsb(ish);
		isb();
		guard->active = false;
		h->guard_count--;
		return -EFAULT;
	}
	return 0;
}

static void hk_bti_guard_restore(struct hk_inline *h)
{
	unsigned long *ptep;
	u32 i;

	for (i = 0; i < h->guard_count; i++) {
		if (!h->guard[i].active)
			continue;
		ptep = h->guard[i].ptep;
		if (!ptep)
			continue;
		WRITE_ONCE(*ptep, h->guard[i].pte);
		__flush_tlb_kernel_pgtable(h->guard[i].page);
		dsb(ish);
		isb();
		h->guard[i].active = false;
	}
	h->guard_count = 0;
}

static __nocfi int hk_inline_apply(struct hk_inline *h, const char *sym,
				   const char *wrapper_sym, unsigned long addr)
{
	struct hk_relo_ctx ctx;
	u32 *tramp;
	u32 detour[HK_INLINE_ENTRY_MAX];
	unsigned long wrapper;
	unsigned long mem;
	u32 saved[HK_INLINE_ENTRY_MAX];
	u32 i;
	int ret;

	memset(h, 0, sizeof(*h));
	tramp = kzalloc(256, GFP_KERNEL);
	if (!tramp)
		return -ENOMEM;

	wrapper = hk_resolve(wrapper_sym);
	if (!wrapper || !hk_ker_addr_ok(wrapper)) {
		pr_warn("[lkmhook] inline resolve %s failed\n", wrapper_sym);
		ret = -ENODATA;
		goto err_free;
	}

	/*
	 * the window is read once through the nofault helper and every later step
	 * uses the copy, the entry can be restored to its own bytes by a failed
	 * install after this point and the relocation must not read the patched
	 * text again
	 */
	for (i = 0; i < HK_INLINE_ENTRY_MAX; i++) {
		ret = hk_fetch_insn(addr + i * 4, &saved[i]);
		if (ret) {
			pr_warn("[lkmhook] inline %s entry is not readable\n",
				sym);
			ret = -EFAULT;
			goto err_free;
		}
		h->saved[i * 4] = saved[i] & 0xFF;
		h->saved[i * 4 + 1] = (saved[i] >> 8) & 0xFF;
		h->saved[i * 4 + 2] = (saved[i] >> 16) & 0xFF;
		h->saved[i * 4 + 3] = (saved[i] >> 24) & 0xFF;
	}
	ret = hk_inline_short(saved, HK_INLINE_ENTRY_MAX);
	if (ret) {
		pr_warn("[lkmhook] inline %s is shorter than the window, breakpoint route\n",
			sym);
		goto err_free;
	}

	mem = (unsigned long)hk_exec_alloc(HK_TRAMP_SIZE);
	if (!mem) {
		pr_warn("[lkmhook] inline exec alloc %s failed\n", sym);
		ret = -ENOMEM;
		goto err_free;
	}
	h->mem = (void *)mem;
	h->mem_size = HK_TRAMP_SIZE;
	h->addr = addr;
	h->window = HK_INLINE_PATCH_LEN;
	h->name = sym;

	tramp[0] = HK_INS_BTI_JC;
	tramp[1] = HK_INS_LDR_X17;
	tramp[2] = HK_INS_RET_X17;
	tramp[3] = wrapper & 0xFFFFFFFF;
	tramp[4] = wrapper >> 32;
	tramp[5] = HK_INS_BTI_JC;

	memset(&ctx, 0, sizeof(ctx));
	ctx.dst = tramp + 6;
	ctx.src = h->saved;
	ctx.tramp_start = addr;
	ctx.tramp_end = addr + HK_INLINE_PATCH_LEN;
	ctx.backup_start = mem + 24;

	for (i = 0; i < HK_INLINE_ENTRY_MAX; i++) {
		ctx.inst_addr = addr + i * 4;
		ret = hk_relo_inst(&ctx, saved[i]);
		if (ret) {
			pr_warn("[lkmhook] inline relo %s insn %u refused %d\n",
				sym, i, ret);
			goto err_guard;
		}
	}

	/*
	 * the continuation is the instruction after the window. it is reached with
	 * an indirect branch, so the page is guarded and the landing pad rule
	 * applies: br x17 stays when the attribute can be cleared, otherwise ret
	 * x17 is used, which is a return and has no landing pad requirement
	 */
	ctx.inst_addr = addr + HK_INLINE_PATCH_LEN;
	h->use_br_x17 = true;
	if (hk_bti_guard_track(h, addr + HK_INLINE_PATCH_LEN))
		h->use_br_x17 = false;
	ret = hk_relo_stub(&ctx, addr + HK_INLINE_PATCH_LEN, h->use_br_x17);
	if (ret)
		goto err_guard;

	ret = hk_engine_write((void *)mem, tramp, ctx.count * 4 + 24);
	if (ret)
		goto err_guard;

	h->orig = mem + 20;

	/*
	 * the detour is written in one call, so a multi instruction window goes
	 * through stop_machine: no core is running while the entry changes from
	 * the caller's bytes to bti jc plus an absolute jump
	 */
	detour[0] = HK_INS_BTI_JC;
	detour[1] = HK_INS_LDR_X17;
	detour[2] = HK_INS_RET_X17;
	detour[3] = mem & 0xFFFFFFFF;
	detour[4] = mem >> 32;
	ret = hk_engine_write((void *)addr, detour, HK_INLINE_PATCH_LEN);
	if (ret) {
		hk_engine_write((void *)addr, h->saved, HK_INLINE_PATCH_LEN);
		goto err_guard;
	}

	kfree(tramp);
	pr_info("[lkmhook] inline %s addr=0x%lx tramp=0x%lx words=%u br_x17=%u\n",
		sym, addr, mem, ctx.count, h->use_br_x17 ? 1 : 0);
	return 0;

err_guard:
	hk_bti_guard_restore(h);
	hk_exec_free(h->mem);
	h->mem = NULL;
	pr_warn("[lkmhook] inline %s failed %d\n", sym, ret);
err_free:
	kfree(tramp);
	return ret;
}

__nocfi int hk_inline_hook(struct hk_inline *h, const char *sym,
			   const char *wrapper_sym)
{
	unsigned long addr = 0;
	int ret;

	if (!h || !sym || !wrapper_sym)
		return -EINVAL;

	/*
	 * one lock across trampoline allocation, relocation and both text
	 * writes, so two installs cannot interleave their trampolines or their
	 * entry windows, and no trampoline is freed under a reader
	 */
	ret = hk_read_addr(sym, &addr);
	if (ret) {
		pr_warn("[lkmhook] inline resolve %s failed %d\n", sym, ret);
		return -ENODATA;
	}
	ret = hk_inline_own(h, sym, addr, HK_INLINE_PATCH_LEN);
	if (ret)
		return ret;
	mutex_lock(&g_inline_lock);
	ret = hk_inline_apply(h, sym, wrapper_sym, addr);
	mutex_unlock(&g_inline_lock);
	if (ret)
		hk_inline_release(h);
	return ret;
}

int hk_inline_disable(struct hk_inline *h)
{
	/* the restore is a text write too, keep it off a concurrent install */
	mutex_lock(&g_inline_lock);
	if (!h || !h->addr) {
		mutex_unlock(&g_inline_lock);
		return -EINVAL;
	}
	if (h->disabled) {
		mutex_unlock(&g_inline_lock);
		return 0;
	}
	if (hk_engine_write((void *)h->addr, h->saved, HK_INLINE_PATCH_LEN)) {
		mutex_unlock(&g_inline_lock);
		return -EIO;
	}
	h->disabled = true;
	mutex_unlock(&g_inline_lock);

	/* the entry is the caller's bytes again, the window is up for grabs */
	hk_inline_release(h);
	return 0;
}

/*
 * free the trampoline. the entry must be the caller's bytes again and the
 * guarded pages go back to what they were. a hook that never reached disable is
 * refused, its detour is still the entry
 */
void hk_inline_free(struct hk_inline *h)
{
	mutex_lock(&g_inline_lock);
	if (!h) {
		mutex_unlock(&g_inline_lock);
		return;
	}
	if (h->addr && !h->disabled) {
		mutex_unlock(&g_inline_lock);
		/*
		 * freeing before the disable is a programmer error: the detour is
		 * still the entry, so the free is refused and the trampoline stays.
		 * the notice is what a caller that wants to know about it reads
		 */
		hk_notice(HK_NOTICE_FREE_BEFORE_DISABLE,
			  h->name ? h->name : "unnamed hook");
		return;
	}
	hk_bti_guard_restore(h);
	if (h->mem)
		hk_exec_free(h->mem);
	h->addr = 0;
	h->orig = 0;
	h->mem = NULL;
	h->disabled = false;
	mutex_unlock(&g_inline_lock);

	/*
	 * hk_inline_disable already handed the window back, this is the release
	 * for a hook that never reached disable. the node keeps no bytes, the
	 * entry still carries the last detour, so a claim taken here covers
	 * bytes the caller has to restore itself
	 */
	hk_inline_release(h);
}

static void hk_inline_pending_add(struct hk_inline *h)
{
	struct hk_inline_pending *ent;

	mutex_lock(&g_inline_lock);
	list_for_each_entry(ent, &g_inline_pending, list) {
		if (ent->hook == h) {
			mutex_unlock(&g_inline_lock);
			return;
		}
	}
	ent = kzalloc(sizeof(*ent), GFP_KERNEL);
	if (!ent) {
		mutex_unlock(&g_inline_lock);
		return;
	}
	ent->hook = h;
	list_add(&ent->list, &g_inline_pending);
	mutex_unlock(&g_inline_lock);
}

static void hk_inline_pending_del(struct hk_inline *h)
{
	struct hk_inline_pending *ent;

	mutex_lock(&g_inline_lock);
	list_for_each_entry(ent, &g_inline_pending, list) {
		if (ent->hook != h)
			continue;
		list_del(&ent->list);
		mutex_unlock(&g_inline_lock);
		kfree(ent);
		return;
	}
	mutex_unlock(&g_inline_lock);
}

/*
 * the three stage unload. disable first, so no core can fetch the detour again.
 * then one tasks RCU grace period, which is what waits for a core that already
 * fetched the detour and is inside the trampoline, the kernel uses the same
 * flavour for its own text pokes. the free comes last, after both. a hook whose
 * entry could not be restored stays live and pending for hk_inline_exit
 */
void hk_inline_unhook(struct hk_inline *h)
{
	if (!h)
		return;
	if (hk_inline_disable(h)) {
		h->pending = true;
		hk_inline_pending_add(h);
		pr_warn("[lkmhook] inline unhook %s parked, the entry is still patched\n",
			h->name ? h->name : "?");
		return;
	}
	hk_wait_tasks_rcu();
	hk_inline_free(h);
	h->pending = false;
	hk_inline_pending_del(h);
}

unsigned int hk_inline_pending(void)
{
	unsigned int count = 0;
	struct hk_inline_pending *ent;

	mutex_lock(&g_inline_lock);
	list_for_each_entry(ent, &g_inline_pending, list)
		count++;
	mutex_unlock(&g_inline_lock);
	return count;
}

void hk_inline_exit(void)
{
	struct hk_inline_pending *ent;
	struct hk_inline_pending *tmp;
	LIST_HEAD(work);

	mutex_lock(&g_inline_lock);
	list_splice_init(&g_inline_pending, &work);
	mutex_unlock(&g_inline_lock);

	list_for_each_entry_safe(ent, tmp, &work, list) {
		list_del(&ent->list);
		if (hk_inline_disable(ent->hook)) {
			pr_warn("[lkmhook] inline %s still patched\n",
				ent->hook->name ? ent->hook->name : "?");
			list_add(&ent->list, &work);
			continue;
		}
		hk_wait_tasks_rcu();
		hk_inline_free(ent->hook);
		ent->hook->pending = false;
		kfree(ent);
	}

	mutex_lock(&g_inline_lock);
	list_splice_tail(&work, &g_inline_pending);
	mutex_unlock(&g_inline_lock);
}

/* the same walk as hk_resolve_branch_chain, which reads the target directly.
 * a thunk is a supported hook target, here it is a report */
static __nocfi int hk_follow_branch(unsigned long addr, unsigned long *out)
{
	return hk_resolve_branch_chain(addr, out);
}

static __nocfi bool hk_is_br(u32 insn)
{
	return (insn & HK_INS_BR_MASK) == HK_INS_BR_ANY;
}

static __nocfi bool hk_is_brk(u32 insn)
{
	return (insn & HK_INS_BRK_MASK) == HK_INS_BRK_ANY;
}

/* hint space, wider than hk_is_hint: bti is outside that mask and the detour
 * this library writes leads with bti jc */
static __nocfi bool hk_is_pad(u32 insn)
{
	return (insn & 0xFFFFF01F) == 0xD503201F;
}

/* br carries its register in rn, the literal loads and the moves in rd */
static __nocfi u32 hk_stub_rn(u32 insn)
{
	return (insn >> 5) & 0x1F;
}

static __nocfi u32 hk_stub_rd(u32 insn)
{
	return insn & 0x1F;
}

/*
 * exact stubs only. the relocation tables leave br, brk, movz and movk out, so
 * they are spelled out here, and anything else has to stay plain: a stub that
 * passes for a prologue is the answer a caller cannot recover from
 */
static __nocfi enum hk_inline_state hk_stub_state(const u32 *win,
						  const char **reason)
{
	const u32 *s = win;
	int left = HK_INLINE_ENTRY_MAX;
	u32 rd;
	int i;

	/* the ftrace preamble, every instrumented entry carries it, and a live
	 * one is patched to a bl that a detour would sit on top of */
	if (win[0] == HK_INS_MOV_X9_LR) {
		*reason = hk_insn_type(win[1]) == HK_INST_BL ?
			  "ftrace call site at entry" : "ftrace patch site at entry";
		return HK_INLINE_PATCHSITE;
	}
	if (hk_is_pad(win[0])) {
		s++;
		left--;
	}
	rd = hk_stub_rd(s[0]);
	if (hk_is_br(s[0])) {
		*reason = "br at entry";
		return HK_INLINE_HOOKED;
	}
	if (hk_is_brk(s[0])) {
		*reason = "brk at entry";
		return HK_INLINE_HOOKED;
	}
	/* a b behind a pad the branch walk does not cross, bti is outside
	 * hk_is_hint, so the hook would patch this entry and not its target */
	if (hk_is_b(s[0])) {
		*reason = "direct b at entry";
		return HK_INLINE_BRANCHED;
	}
	if (hk_insn_type(s[0]) == HK_INST_LDR_64 && hk_is_br(s[1]) &&
	    hk_stub_rn(s[1]) == rd) {
		*reason = "ldr br stub at entry";
		return HK_INLINE_HOOKED;
	}
	if ((s[0] & HK_INS_MOV_MASK) == HK_INS_MOVZ) {
		for (i = 1; i < left; i++) {
			if (hk_is_br(s[i]) && hk_stub_rn(s[i]) == rd) {
				*reason = "movz br stub at entry";
				return HK_INLINE_HOOKED;
			}
			if ((s[i] & HK_INS_MOV_MASK) != HK_INS_MOVK ||
			    hk_stub_rd(s[i]) != rd)
				break;
		}
	}
	return HK_INLINE_PLAIN;
}

__nocfi int hk_inline_probe(const char *sym, struct hk_inline_probe *out)
{
	struct hk_relo_ctx ctx;
	u8 raw[HK_INLINE_PATCH_LEN];
	u32 win[HK_INLINE_ENTRY_MAX];
	u32 scratch[HK_PROBE_SCRATCH];
	unsigned long addr;
	unsigned long target;
	u32 i;
	int ret;

	if (!sym || !out)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	out->patch_len = HK_INLINE_PATCH_LEN;
	out->state = HK_INLINE_UNRESOLVED;
	out->reason = "symbol not resolved";

	addr = hk_resolve(sym);
	if (!addr)
		return 0;
	if (!hk_ker_addr_ok(addr)) {
		out->state = HK_INLINE_UNSUPPORTED;
		out->reason = "not a kernel address";
		return 0;
	}
	out->addr = addr;

	if (hk_follow_branch(addr, &target)) {
		out->state = HK_INLINE_UNREADABLE;
		out->reason = "entry not readable";
		return 0;
	}
	if (!hk_ker_addr_ok(target)) {
		out->state = HK_INLINE_UNSUPPORTED;
		out->reason = "not a kernel address";
		return 0;
	}
	out->target = target;

	/* the entry is not judged further, an install onto it is refused */
	if (hk_inline_collide(sym, target, HK_INLINE_PATCH_LEN,
			      &out->collide_addr)) {
		out->state = HK_INLINE_COLLISION;
		out->reason = "window owned by a live hook";
		return 0;
	}

	if (hk_read_code(target, raw, sizeof(raw))) {
		out->state = HK_INLINE_UNREADABLE;
		out->reason = "entry not readable";
		return 0;
	}
	for (i = 0; i < HK_INLINE_ENTRY_MAX; i++)
		win[i] = hk_get_insn(raw + i * 4);

	out->state = hk_stub_state(win, &out->reason);
	if (out->state != HK_INLINE_PLAIN)
		return 0;

	/* dry run the relocator over the copy, scratch is never patched and
	 * never mapped executable, only the refusal is of interest */
	memset(&ctx, 0, sizeof(ctx));
	ctx.dst = scratch;
	ctx.src = raw;
	ctx.tramp_start = target;
	ctx.tramp_end = target + HK_INLINE_PATCH_LEN;
	for (i = 0; i < HK_INLINE_ENTRY_MAX; i++) {
		ctx.inst_addr = target + i * 4;
		ret = hk_relo_inst(&ctx, win[i]);
		if (ret) {
			out->state = HK_INLINE_UNSUPPORTED;
			out->reason = "entry operand points into the patched window";
			return 0;
		}
	}

	if (target != addr) {
		out->state = HK_INLINE_BRANCHED;
		out->reason = "direct b at entry";
		return 0;
	}
	out->state = HK_INLINE_PLAIN;
	out->reason = "no branch stub found";
	return 0;
}
