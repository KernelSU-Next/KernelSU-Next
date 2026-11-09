/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/compiler.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <uapi/linux/btf.h>

#include "btf_layout.h"
#include "symbol_resolver.h"

struct btf;

static struct btf *susfs_core_btf;
static const struct btf_type *(*susfs_btf_type_fn)(const struct btf *, u32);
static const char *(*susfs_btf_name_fn)(const struct btf *, u32);
static u32 (*susfs_btf_count_fn)(const struct btf *);
static u32 susfs_btf_count;

static const struct btf_type *__nocfi susfs_btf_type(u32 id)
{
    if (!id || id >= susfs_btf_count)
        return NULL;
    return susfs_btf_type_fn(susfs_core_btf, id);
}

static const char *__nocfi susfs_btf_name(u32 offset)
{
    return susfs_btf_name_fn(susfs_core_btf, offset);
}

static u32 __nocfi susfs_btf_type_count(void)
{
    return susfs_btf_count_fn(susfs_core_btf);
}

static u32 susfs_btf_unwrap(u32 id)
{
    unsigned int i;

    for (i = 0; id && i < 32; i++) {
        const struct btf_type *t = susfs_btf_type(id);
        u32 kind;

        if (!t)
            return 0;
        kind = BTF_INFO_KIND(t->info);
        if (kind != BTF_KIND_TYPEDEF && kind != BTF_KIND_CONST &&
            kind != BTF_KIND_VOLATILE && kind != BTF_KIND_RESTRICT && kind != 18)
            return id;
        id = t->type;
    }
    return 0;
}

static bool susfs_btf_named(const struct btf_type *t, const char *name, u32 kind)
{
    const char *actual;

    if (!t || BTF_INFO_KIND(t->info) != kind)
        return false;
    actual = susfs_btf_name(t->name_off);
    return actual && !strcmp(actual, name);
}

static const struct btf_member *susfs_btf_member(const struct btf_type *t,
                                               const char *name)
{
    const struct btf_member *members = (const void *)(t + 1);
    const struct btf_member *found = NULL;
    u32 i;

    for (i = 0; i < BTF_INFO_VLEN(t->info); i++) {
        const char *actual = susfs_btf_name(members[i].name_off);

        if (!actual || strcmp(actual, name))
            continue;
        if (found || (BTF_INFO_KFLAG(t->info) &&
                      BTF_MEMBER_BITFIELD_SIZE(members[i].offset)))
            return NULL;
        found = &members[i];
    }
    return found;
}

static u32 susfs_btf_member_bits(const struct btf_type *t, const struct btf_member *m)
{
    return BTF_INFO_KFLAG(t->info) ? BTF_MEMBER_BIT_OFFSET(m->offset) : m->offset;
}

static bool susfs_btf_matches(u32 id, const char *wanted)
{
    const struct btf_type *t;
    const char *name;
    char base[64];
    size_t length = strlen(wanted), pointers = 0, i;

    while (length && wanted[length - 1] == '*') {
        pointers++;
        length--;
    }
    if (!length || length >= sizeof(base))
        return false;
    memcpy(base, wanted, length);
    base[length] = '\0';
    for (i = 0; i < pointers; i++) {
        id = susfs_btf_unwrap(id);
        t = susfs_btf_type(id);
        if (!t || BTF_INFO_KIND(t->info) != BTF_KIND_PTR)
            return false;
        id = t->type;
    }
    if (!strcmp(base, "void"))
        return !susfs_btf_unwrap(id);
    for (i = 0; id && i < 32; i++) {
        u32 kind;

        t = susfs_btf_type(id);
        if (!t)
            return false;
        kind = BTF_INFO_KIND(t->info);
        name = susfs_btf_name(t->name_off);
        if (kind == BTF_KIND_TYPEDEF && name && !strcmp(name, base))
            return true;
        if (kind == BTF_KIND_TYPEDEF || kind == BTF_KIND_CONST ||
            kind == BTF_KIND_VOLATILE || kind == BTF_KIND_RESTRICT || kind == 18) {
            id = t->type;
            continue;
        }
        if (!strcmp(base, "int") || !strcmp(base, "uint")) {
            u32 encoding;

            if (kind != BTF_KIND_INT || t->size != sizeof(u32))
                return false;
            encoding = *(const u32 *)(t + 1);
            return BTF_INT_BITS(encoding) == 32 &&
                   BTF_INT_OFFSET(encoding) == 0 &&
                   !!(BTF_INT_ENCODING(encoding) & BTF_INT_SIGNED) ==
                       !strcmp(base, "int");
        }
        return kind == BTF_KIND_STRUCT && name && !strcmp(name, base);
    }
    return false;
}

static int susfs_btf_proto(u32 id, const char *result,
                          const char *const *params, size_t count)
{
    const struct btf_type *t = susfs_btf_type(susfs_btf_unwrap(id));
    const struct btf_param *args;
    size_t i;

    if (!t || BTF_INFO_KIND(t->info) != BTF_KIND_FUNC_PROTO ||
        BTF_INFO_VLEN(t->info) != count ||
        !susfs_btf_matches(t->type, result))
        return -EINVAL;
    args = (const void *)(t + 1);
    for (i = 0; i < count; i++) {
        if (!susfs_btf_matches(args[i].type, params[i]))
            return -EINVAL;
    }
    return 0;
}

int susfs_btf_layout_init(void)
{
    unsigned long address = find_kernel_symbol_exact("btf_vmlinux");

    if (!address || copy_from_kernel_nofault(&susfs_core_btf,
                                            (const void *)address,
                                            sizeof(susfs_core_btf)) ||
        IS_ERR_OR_NULL(susfs_core_btf))
        return -EOPNOTSUPP;
    susfs_btf_type_fn = ksu_resolve_symbol_for_functable_hook("btf_type_by_id");
    susfs_btf_name_fn = ksu_resolve_symbol_for_functable_hook("btf_name_by_offset");
    susfs_btf_count_fn = ksu_resolve_symbol_for_functable_hook("btf_nr_types");
    if (!susfs_btf_type_fn || !susfs_btf_name_fn)
        return -EOPNOTSUPP;
    if (susfs_btf_count_fn) {
        susfs_btf_count = susfs_btf_type_count();
    } else {
        u32 id;

        susfs_btf_count = 1048576;
        for (id = 1; id < susfs_btf_count; id++) {
            if (!susfs_btf_type(id))
                break;
        }
        if (id == susfs_btf_count)
            return -E2BIG;
        susfs_btf_count = id;
    }
    if (!susfs_btf_count || susfs_btf_count > 1048576)
        return -EINVAL;
    return 0;
}

int susfs_btf_struct_size(const char *name, size_t *size)
{
    u32 id;
    bool found = false;
    size_t actual = 0;

    for (id = 1; id < susfs_btf_count; id++) {
        const struct btf_type *t = susfs_btf_type(id);
        if (!susfs_btf_named(t, name, BTF_KIND_STRUCT))
            continue;
        if (!t->size || t->size > 4096 || (found && actual != t->size))
            return -EINVAL;
        actual = t->size;
        found = true;
    }
    if (!found)
        return -ENOENT;
    *size = actual;
    return 0;
}

int susfs_btf_field_offset(const char *name, const char *member,
                          const char *type, size_t *offset)
{
    u32 id;
    bool found = false;
    size_t actual = 0;

    for (id = 1; id < susfs_btf_count; id++) {
        const struct btf_type *t = susfs_btf_type(id);
        const struct btf_member *m;
        u32 bits;

        if (!susfs_btf_named(t, name, BTF_KIND_STRUCT))
            continue;
        m = susfs_btf_member(t, member);
        if (!m)
            return -ENOENT;
        bits = susfs_btf_member_bits(t, m);
        if (bits % (sizeof(void *) * 8) ||
            bits / 8 > t->size ||
            t->size - bits / 8 < sizeof(void *) ||
            !susfs_btf_matches(m->type, type) ||
            (found && actual != bits / 8))
            return -EINVAL;
        actual = bits / 8;
        found = true;
    }
    if (!found)
        return -ENOENT;
    *offset = actual;
    return 0;
}

/* Same lookup as susfs_btf_field_offset(), without that one's pointer-only shape: it exists for
 * scalar members too (SELinux's inode_security_struct.sid is a u32).  The member's type still has
 * to match the name given, so a layout that moved the field cannot be mistaken for the right one. */
int susfs_btf_member_offset(const char *name, const char *member,
                            const char *type, size_t *offset)
{
    u32 id;
    bool found = false;
    size_t actual = 0;

    for (id = 1; id < susfs_btf_count; id++) {
        const struct btf_type *t = susfs_btf_type(id);
        const struct btf_member *m;
        u32 bits;

        if (!susfs_btf_named(t, name, BTF_KIND_STRUCT))
            continue;
        m = susfs_btf_member(t, member);
        if (!m)
            return -ENOENT;
        bits = susfs_btf_member_bits(t, m);
        if (bits % 8 || bits / 8 > t->size ||
            !susfs_btf_matches(m->type, type) ||
            (found && actual != bits / 8))
            return -EINVAL;
        actual = bits / 8;
        found = true;
    }
    if (!found)
        return -ENOENT;
    *offset = actual;
    return 0;
}

int susfs_btf_enum_value(const char *name, const char *member, int *value)
{
    u32 id;
    bool found = false;
    int actual_value = 0;

    for (id = 1; id < susfs_btf_count; id++) {
        const struct btf_type *t = susfs_btf_type(id);
        const struct btf_enum *values;
        u32 i;
        bool matched = false;

        if (!susfs_btf_named(t, name, BTF_KIND_ENUM))
            continue;
        values = (const void *)(t + 1);
        for (i = 0; i < BTF_INFO_VLEN(t->info); i++) {
            const char *actual = susfs_btf_name(values[i].name_off);

            if (!actual || strcmp(actual, member))
                continue;
            if (matched || (found && values[i].val != actual_value))
                return -EINVAL;
            actual_value = values[i].val;
            matched = true;
        }
        if (!matched)
            return -ENOENT;
        found = true;
    }
    if (!found)
        return -ENOENT;
    *value = actual_value;
    return 0;
}

int susfs_btf_check_function(const char *name, const char *result,
                            const char *const *params, size_t count)
{
    u32 id;
    bool found = false;

    for (id = 1; id < susfs_btf_count; id++) {
        const struct btf_type *t = susfs_btf_type(id);

        if (!susfs_btf_named(t, name, BTF_KIND_FUNC))
            continue;
        found = true;
        if (susfs_btf_proto(t->type, result, params, count))
            return -EINVAL;
    }
    return found ? 0 : -ENOENT;
}

int susfs_btf_callback_offset(const char *name, const char *member,
                             const char *result, const char *const *params,
                             size_t count, size_t *offset)
{
    u32 id;
    bool found = false;
    size_t actual = 0;

    for (id = 1; id < susfs_btf_count; id++) {
        const struct btf_type *t = susfs_btf_type(id);
        const struct btf_member *m;
        const struct btf_type *pointer;
        u32 bits;

        if (!susfs_btf_named(t, name, BTF_KIND_STRUCT))
            continue;
        m = susfs_btf_member(t, member);
        if (!m)
            return -ENOENT;
        bits = susfs_btf_member_bits(t, m);
        if (bits % (sizeof(void *) * 8) ||
            bits / 8 > t->size ||
            t->size - bits / 8 < sizeof(void *) ||
            (found && actual != bits / 8))
            return -EINVAL;
        pointer = susfs_btf_type(susfs_btf_unwrap(m->type));
        if (!pointer || BTF_INFO_KIND(pointer->info) != BTF_KIND_PTR ||
            susfs_btf_proto(pointer->type, result, params, count))
            return -EINVAL;
        actual = bits / 8;
        found = true;
    }
    if (!found)
        return -ENOENT;
    *offset = actual;
    return 0;
}
