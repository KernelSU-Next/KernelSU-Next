/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_enable_log_data.h - logging switch (D layer)
 */

#ifndef __SUSFS_ENABLE_LOG_DATA_H
#define __SUSFS_ENABLE_LOG_DATA_H

#include <linux/types.h>

extern bool susfs_log_on;
bool susfs_log_enabled(void);

#endif /* __SUSFS_ENABLE_LOG_DATA_H */
