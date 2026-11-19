/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_open_data.h - D facts for the open entry
 *
 * Looks up the rule table and copies the matched rule's facts.  It does not
 * read creds or decide whether the caller may be redirected.
 */

#ifndef __SUSFS_OPEN_DATA_H
#define __SUSFS_OPEN_DATA_H

#include <linux/types.h>

#define SUSFS_OPEN_DATA_PATH_MAX 256

struct path;

struct susfs_open_rule_facts {
	bool matched;
	int uid_scheme;
	char redirected[SUSFS_OPEN_DATA_PATH_MAX];
};

int susfs_open_rule_facts_path(const struct path *path,
			       struct susfs_open_rule_facts *facts);

#endif /* __SUSFS_OPEN_DATA_H */
