/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SUSFS_HELPERS_H
#define __SUSFS_HELPERS_H

#include <linux/types.h>
#include <linux/string.h>
#include <linux/err.h>
#include <linux/mm.h>

/* A kernel pointer taken out of a kprobe register, checked before dereference: single_open()'s fake inode is (void *)1
 * and a kretprobe hands that sentinel over as "the argument" - the sus_map vma probe panicked on exactly that (fault
 * address 0xa1) until this test existed.  Every kernel object lives above the first page, so "< PAGE_SIZE" covers a
 * sentinel and a garbage register too, and IS_ERR() covers error pointers; user pointers must NOT go through it. */
static inline bool susfs_ptr_plausible(const void *p)
{
	return (unsigned long)p >= PAGE_SIZE && !IS_ERR(p);
}

/* Bind a fixed-size ABI pathname field to a C string safely: char[N] pathname fields a caller need not NUL-terminate, and
 * strlen()/strcmp()/kern_path() on one walks off the end of the struct, which lives on OUR kernel stack.  Every consumer of
 * an ABI pathname goes through this first. */
static inline bool susfs_abi_path_ok(const char *field, size_t size)
{
	return strnlen(field, size) < size;
}

#endif /* __SUSFS_HELPERS_H */
