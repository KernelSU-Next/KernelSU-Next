// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef LKMHOOK_HK_PTR_H
#define LKMHOOK_HK_PTR_H

#include <linux/types.h>

/*
 * swap the pointer at slot for replacement and record the original for
 * hk_ptr_unhook or hk_ptr_exit. the duplicate check and the record are under one
 * spinlock and the swap is not: the swap goes through the fixmap and a fixmap
 * open sleeps, so it runs from an entry already reserved under the lock. a slot
 * is taken from the moment it is reserved, a second hook on it is -EEXIST
 *
 * the hooks live in a list, there is no fixed capacity and no -ENOSPC. the walk
 * over the live hooks costs the same as the old table scan. the tracking entry is
 * allocated, so this runs in process context, and -ENOMEM is the new full case
 *
 * -EINVAL on a NULL slot or replacement, -EEXIST when the slot is already hooked,
 * -EFAULT when the slot cannot be read, -EIO when the write fails
 */
int hk_ptr_hook(void **slot, void *replacement, void **orig_out);

/* restore the recorded original, silent when the slot is not hooked */
void hk_ptr_unhook(void **slot);

/*
 * what a consumer compiles in is its own build decision. setting the flag leaves
 * this module out of the link, and the calls the core makes into it become
 * nothing, while the entry points of the module itself stay declared: calling one
 * of those without its object is a link error, never a silent no op
 */
#ifdef HK_NO_PTR
static inline void hk_ptr_exit(void) { }
static inline unsigned int hk_ptr_pending(void) { return 0; }
#else
/* restore every tracked slot, called by hk_exit */
void hk_ptr_exit(void);

/* slots whose original could not be written back, counted for the exit gate */
unsigned int hk_ptr_pending(void);
#endif
#endif
