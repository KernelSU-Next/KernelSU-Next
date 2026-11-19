/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __SUSFS_HIDE_PHYS_H
#define __SUSFS_HIDE_PHYS_H
#include <linux/types.h>
struct susfs_hook;
extern void susfs_hide_s_stub(void);
extern void susfs_hide_m_stub(void);
extern unsigned long susfs_hide_s_tramp;
extern unsigned long susfs_hide_m_tramp;
void hide_modules_sync_sysfs(void);
int susfs_hide_phys_init(struct susfs_hook *s_hook, struct susfs_hook *m_hook);
void susfs_hide_phys_exit(struct susfs_hook *s_hook, struct susfs_hook *m_hook);
bool susfs_hide_phys_active(void);
#endif
