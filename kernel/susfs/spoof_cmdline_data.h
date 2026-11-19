/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * spoof_cmdline_data.h - bootconfig spoof state (D layer)
 *
 * The insmod parameter buffer, the pointer published into the kernel and the
 * buffers a reader may still be holding.  No decision and no kernel write here.
 */

#ifndef __SPOOF_CMDLINE_DATA_H
#define __SPOOF_CMDLINE_DATA_H

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/list.h>

#define SPOOF_CMDLINE_PARAM_LEN 4096

struct retired_str {
	struct list_head list;
	char *s;
};

extern char spoof_cmdline_param[SPOOF_CMDLINE_PARAM_LEN];
extern char *spoof_cmdline_orig;
extern char *spoof_cmdline_live;
extern bool spoof_cmdline_active;
extern struct mutex spoof_cmdline_lock;
extern struct list_head spoof_cmdline_retired;

void spoof_cmdline_data_init(void);

#endif /* __SPOOF_CMDLINE_DATA_H */
