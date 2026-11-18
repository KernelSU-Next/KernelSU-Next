/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __SUSFS_UNAME_VALUES_H
#define __SUSFS_UNAME_VALUES_H

/* Wire width of new_utsname; checked against the kernel type in P. */
#define SUSFS_UNAME_LEN 65

struct susfs_uname_values {
	char release[SUSFS_UNAME_LEN];
	char version[SUSFS_UNAME_LEN];
};

#endif
