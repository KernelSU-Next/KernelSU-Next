/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kallsyms.h>
#include <linux/string.h>
#include <linux/version.h>

#include "symbol_resolver.h"
#include "susfs_log.h"

#include "kallrecon/lib/core.h"
#include "kallrecon/lib/hint.h"
#include "kallrecon/lib/slide.h"
#include "kallrecon/lib/symbol.h"
#include "kallrecon/lib/variant.h"

#define KR_SCAN_BACK	0x400000UL
#define KR_SCAN_FWD	0x200000UL
#define KR_TIMEOUT_MS	10000

#define KR_NAME_MAX	256
#define KR_ALL_MAX	8

static bool kr_ready;

static bool kr_layout_ready(void)
{
	return klnum_val && kloffs_addr && klnames_addr;
}

static __nocfi noinline unsigned long kr_klp(const char *name)
{
	return kallrecon_klp(name);
}

static const char *(*kr_lookup_fn)(unsigned long addr, unsigned long *symbolsize,
				   unsigned long *offset, char **modname, char *namebuf);

static __nocfi noinline const char *kr_lookup(unsigned long addr, char **modname, char *buf)
{
	return kr_lookup_fn(addr, NULL, NULL, modname, buf);
}

unsigned long find_kernel_symbol_exact_any(const char *symbol_name)
{
	if (!symbol_name || !symbol_name[0] || !kr_ready || !kallrecon_klp)
		return 0;

	return kr_klp(symbol_name);
}

unsigned long ksu_kallsyms_lookup_name(const char *name)
{
	return find_kernel_symbol_exact_any(name);
}

unsigned long find_kernel_symbol_exact(const char *symbol_name)
{
	unsigned long addr;
	char buf[KSYM_SYMBOL_LEN];
	char *mod = NULL;

	addr = find_kernel_symbol_exact_any(symbol_name);
	if (!addr || !kr_lookup_fn)
		return 0;

	if (!kr_lookup(addr, &mod, buf))
		return 0;

	if (mod) {
		pr_warn("ignore symbol %s of module %s\n", symbol_name, mod);
		return 0;
	}

	return addr;
}

void *ksu_resolve_symbol_for_functable_hook(const char *symbol_name)
{
	unsigned long addr;

	if (!kr_ready || !symbol_name || !symbol_name[0])
		return NULL;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
	addr = find_kernel_symbol_exact(symbol_name);
	if (!addr)
		addr = kallrecon_find_variant(symbol_name, 0);
#else
	{
		char cfi_name[KSYM_NAME_LEN];

		snprintf(cfi_name, sizeof(cfi_name), "%s.cfi_jt", symbol_name);
		addr = find_kernel_symbol_exact(cfi_name);
		if (!addr)
			addr = kallrecon_find_variant(symbol_name, 1);
		if (!addr)
			addr = find_kernel_symbol_exact(symbol_name);
	}
#endif

	return addr ? (void *)addr : NULL;
}

struct kr_collect {
	unsigned long *addrs;
	int max;
	int n;
};

static void kr_collect_add(struct kr_collect *c, unsigned long addr)
{
	int i;

	if (!addr)
		return;
	for (i = 0; i < c->n; i++)
		if (c->addrs[i] == addr)
			return;
	if (c->n < c->max)
		c->addrs[c->n++] = addr;
}

int ksu_find_symbol_all(const char *name, unsigned long *addrs, int max)
{
	struct slide_win w;
	struct kr_collect c = { .addrs = addrs, .max = max };
	char nbuf[KR_NAME_MAX];
	unsigned int idx = 0;
	int retried = 0;

	if (!kr_ready || !name || !name[0] || !addrs || max <= 0)
		return 0;

	mutex_lock(&ks_linear_lock);
	if (safe_read(ti_buf, (void *)klindex_addr, sizeof(ti_buf)) ||
	    safe_read(tt_buf, (void *)kltable_addr, sizeof(tt_buf)) ||
	    slide_init(&w, klnames_addr, KS_WIN_SIZE, KS_WIN_MARGIN))
		goto out;

	while (idx < klnum_val) {
		const unsigned char *enc = slide_ptr(&w, slide_buf);
		unsigned int len = enc[0];
		unsigned int hdr = 1;

		if (len & 0x80) {
			len = (len & 0x7f) | (enc[1] << 7);
			hdr = 2;
		}

		if (len > KR_NAME_MAX || w.off + hdr + len > w.valid) {
			if (retried || slide_advance(&w, (w.valid - w.off) + 4))
				break;
			retried = 1;
			continue;
		}
		retried = 0;

		if (ks_expand_raw(enc, ti_buf, tt_buf, nbuf, sizeof(nbuf)) &&
		    !strcmp(nbuf, name))
			kr_collect_add(&c, sym_addr((int)idx));

		if (slide_advance(&w, hdr + len))
			break;
		idx++;
	}

out:
	mutex_unlock(&ks_linear_lock);
	return c.n;
}

int ksu_symbol_name_of(unsigned long addr, char *buf, char **module_out)
{
	char *mod = NULL;

	if (module_out)
		*module_out = NULL;
	if (!buf)
		return -EINVAL;
	buf[0] = '\0';
	if (!kr_ready)
		return -ENOSYS;
	if (!addr)
		return -EINVAL;

	if (kr_lookup_fn) {
		if (!kr_lookup(addr, &mod, buf))
			return -ENOENT;
		if (module_out)
			*module_out = mod;
		return (int)strlen(buf);
	}

	if (sym_name_at(addr, buf, KSYM_SYMBOL_LEN) < 0)
		return -ENOENT;

	return (int)strlen(buf);
}

int ksu_symbol_is_start(unsigned long addr, const char *name)
{
	char buf[KSYM_SYMBOL_LEN];
	size_t len, nlen;
	int idx;

	if (!kr_ready || !addr || !name || !name[0])
		return 0;

	len = strlen(name);
	if (len >= sizeof(buf))
		return 0;

	idx = sym_name_at(addr, buf, sizeof(buf));
	nlen = strlen(buf);
	if (idx >= 0 && sym_addr(idx) == addr && nlen >= len &&
	    !strncmp(buf, name, len) &&
	    (!buf[len] || buf[len] == '.' || buf[len] == '$') &&
	    !strstr(buf + len, ".cfi_jt"))
		return 1;

	return ksu_addr_is_named(addr, name);
}

int ksu_addr_is_named(unsigned long addr, const char *name)
{
	unsigned long addrs[KR_ALL_MAX];
	int n, i;

	if (!kr_ready || !addr || !name || !name[0])
		return 0;

	n = ksu_find_symbol_all(name, addrs, (int)ARRAY_SIZE(addrs));
	for (i = 0; i < n; i++)
		if (addrs[i] == addr)
			return 1;

	return 0;
}

int __init ksu_init_symbol_resolver(void)
{
	struct kallrecon_hint hint;

	memset(&hint, 0, sizeof(hint));
	hint.timeout_ms = KR_TIMEOUT_MS;
	kallrecon_supply(&hint);

	find_kallsyms_base();

	if (!kr_layout_ready() || !kallrecon_klp) {
		memset(&hint, 0, sizeof(hint));
		hint.scan_back = KR_SCAN_BACK * 2;
		hint.scan_fwd = KR_SCAN_FWD * 2;
		hint.timeout_ms = KR_TIMEOUT_MS;
		kallrecon_supply(&hint);
		kallrecon_retry();
	}

	if (!kr_layout_ready() || !kallrecon_klp) {
		pr_err("symbol_resolver: kallrecon discovery failed (%d, klp=%d), refusing to load\n",
		       kallrecon_fail_reason(), !!kallrecon_klp);
		return -ENODATA;
	}

	kr_ready = true;

	kr_lookup_fn = (void *)kr_klp("kallsyms_lookup");
	if (!kr_lookup_fn)
		pr_warn("symbol_resolver: kallsyms_lookup not resolved, module names unavailable\n");

	SUSFS_LOGI("symbol_resolver: kallrecon layout v%d base=0x%lx num=%u klp=%d\n",
		   (int)kl_layout, kernel_base, klnum_val, !!kallrecon_klp);

	return 0;
}
