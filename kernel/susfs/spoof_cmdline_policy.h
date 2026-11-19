/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * spoof_cmdline_policy.h - bootconfig spoof decision (L layer)
 */

#ifndef __SPOOF_CMDLINE_POLICY_H
#define __SPOOF_CMDLINE_POLICY_H

/* Allocate and publish @fake.  Returns 0 or a negative errno. */
int spoof_cmdline_set(const char *fake);

#endif /* __SPOOF_CMDLINE_POLICY_H */
