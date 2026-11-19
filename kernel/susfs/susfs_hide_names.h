/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __SUSFS_HIDE_NAMES_H
#define __SUSFS_HIDE_NAMES_H
#include <linux/module.h>
#define HIDE_MODULES_MAX 16
#define HIDE_MODULES_CMDLINE (HIDE_MODULES_MAX * (MODULE_NAME_LEN + 1) + 96)
#endif
