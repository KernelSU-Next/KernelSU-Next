/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sus_path_data.h - D facts for the sus_path view
 *
 * The rule table is the data layer.  These functions only say which rule
 * kinds matched; creds/gate decisions stay out of D.
 */

#ifndef __SUS_PATH_DATA_H
#define __SUS_PATH_DATA_H

#include <linux/types.h>

struct inode;

struct sus_path_rule_facts {
	bool matched;
	bool self_protect;
	bool user_rule;
	bool pending;
	bool by_identity;
};

int sus_path_rule_facts_inode(struct inode *inode,
			      struct sus_path_rule_facts *facts);
int sus_path_rule_facts_dirent(u64 ino, const char *name,
			       struct sus_path_rule_facts *facts);

#endif /* __SUS_PATH_DATA_H */
