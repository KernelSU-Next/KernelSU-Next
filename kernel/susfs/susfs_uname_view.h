/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __SUSFS_UNAME_VIEW_H
#define __SUSFS_UNAME_VIEW_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdbool.h>
#endif

#include "susfs_uname_values.h"

enum susfs_uname_input {
	SUSFS_UNAME_VALID,
	SUSFS_UNAME_EMPTY,
	SUSFS_UNAME_UNTERMINATED,
};

static inline enum susfs_uname_input
susfs_uname_validate(const struct susfs_uname_values *v)
{
	unsigned int i;
	bool release_end = false, version_end = false;

	/* Empty fields take precedence even if the other field is unterminated. */
	if (!v->release[0] || !v->version[0])
		return SUSFS_UNAME_EMPTY;
	for (i = 0; i < SUSFS_UNAME_LEN; i++) {
		release_end |= v->release[i] == '\0';
		version_end |= v->version[i] == '\0';
	}
	return release_end && version_end ? SUSFS_UNAME_VALID :
		SUSFS_UNAME_UNTERMINATED;
}

static inline bool susfs_uname_should_apply(long result, bool enabled, bool has_buffer)
{
	return result == 0 && enabled && has_buffer;
}

#endif
