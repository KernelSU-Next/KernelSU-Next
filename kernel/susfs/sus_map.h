/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SUS_MAP_H
#define __SUS_MAP_H

#include <linux/types.h>
#include <linux/compiler_types.h>

/* sus_map's rule node: add/del/clear the mappings the maps/smaps/map_files walk hides.  Root-only
 * through the same su-side gate as the other control nodes. */
#define SUSFS_MAP_NODE "/proc/susfs_map"

int susfs_sus_map_init(void);
void susfs_sus_map_exit(void);

void susfs_sus_map_supercall(void __user **arg);

#endif /* __SUS_MAP_H */
