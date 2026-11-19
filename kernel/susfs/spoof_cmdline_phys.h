/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * spoof_cmdline_phys.h - bootconfig pointer publication (P layer)
 *
 * The only code that writes saved_boot_config and frees retired strings.
 */

#ifndef __SPOOF_CMDLINE_PHYS_H
#define __SPOOF_CMDLINE_PHYS_H

/* Publish @dup as the string /proc/bootconfig prints.  Takes ownership on
 * success; the previous live string is retired, not freed. */
void spoof_cmdline_publish(char *dup);

/* Put the original pointer back and retire (then drain) the live string. */
void spoof_cmdline_restore(void);

#endif /* __SPOOF_CMDLINE_PHYS_H */
