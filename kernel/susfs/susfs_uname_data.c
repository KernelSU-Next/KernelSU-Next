/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/string.h>
#include "susfs_uname_data.h"

struct susfs_uname_values susfs_uname_state_values;
bool susfs_uname_enabled;
bool susfs_uname_registered;
bool susfs_uname_inline;
DEFINE_MUTEX(susfs_uname_ctl_lock);

/* The centre publishes/clears this slot around inline installation. */
susfs_syscall_fn_t susfs_uname_orig;

susfs_syscall_fn_t susfs_uname_orig_get(void)
{
	return susfs_uname_orig;
}

static void uname_pad(char *s, size_t size)
{
	size_t n = strnlen(s, size);

	if (n < size)
		memset(s + n, 0, size - n);
}

void susfs_uname_data_pad(void)
{
	/* param_set_copystring does not zero-pad the tail. */
	uname_pad(susfs_uname_state_values.release, SUSFS_UNAME_LEN);
	uname_pad(susfs_uname_state_values.version, SUSFS_UNAME_LEN);
}
