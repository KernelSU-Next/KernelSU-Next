/* SPDX-License-Identifier: GPL-2.0 */
#ifndef SUSFS_FSNOTIFY_LIFETIME_H
#define SUSFS_FSNOTIFY_LIFETIME_H

struct super_block;

int susfs_sb_watch_init(void (*release)(struct super_block *));
int susfs_sb_watch_add(struct super_block *sb);
void susfs_sb_watch_exit(void);

#endif
