/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sus_path_match.h - SUS_PATH rule facts for the create gate
 *
 * D owns the rule table; it answers only what it can prove from that table.
 * The app/su/self-protect decision stays in the caller's policy layer.
 */

#ifndef __SUS_PATH_MATCH_H
#define __SUS_PATH_MATCH_H

#include <linux/types.h>

struct susfs_create_view;

struct sus_path_match {
	bool rule_matched;
	bool ordinary_rule;
	bool self_protect;
	bool pending;
};

/* Match an already-resolved create target against the registered SUS_PATH rules.
 * Returns 0 when the lookup ran, -errno when it could not.  A miss is not an error:
 * rule_matched stays false. */
int sus_path_rule_match(const struct susfs_create_view *v,
			struct sus_path_match *out);

/* Global policy input: hide_from_apps=0 applies the set to every process. */
bool sus_path_hide_from_apps(void);

#endif /* __SUS_PATH_MATCH_H */
