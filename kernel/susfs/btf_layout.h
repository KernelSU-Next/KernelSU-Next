/* SPDX-License-Identifier: GPL-2.0 */
#ifndef SUSFS_BTF_LAYOUT_H
#define SUSFS_BTF_LAYOUT_H

#include <linux/types.h>

int susfs_btf_layout_init(void);
int susfs_btf_struct_size(const char *name, size_t *size);
int susfs_btf_field_offset(const char *name, const char *member,
                          const char *type, size_t *offset);
int susfs_btf_member_offset(const char *name, const char *member,
                            const char *type, size_t *offset);
int susfs_btf_enum_value(const char *name, const char *member, int *value);
int susfs_btf_check_function(const char *name, const char *result,
                            const char *const *params, size_t count);
int susfs_btf_callback_offset(const char *name, const char *member,
                             const char *result, const char *const *params,
                             size_t count, size_t *offset);

#endif
