#ifndef __KSU_H_UID_OBSERVER
#define __KSU_H_UID_OBSERVER

#include <linux/types.h>
#ifdef CONFIG_KSU_DISABLE_MANAGER
static inline void ksu_throne_tracker_init()
{
}

static inline void ksu_throne_tracker_exit()
{
}

static inline void track_throne(bool prune_only)
{
    (void)prune_only;
}

static inline void ksu_request_manager_scan(bool from_rename)
{
    (void)from_rename;
}
#else
void ksu_throne_tracker_init();

void ksu_throne_tracker_exit();

void track_throne(bool prune_only);

/*
 * Defer a track_throne() scan to a workqueue, from hooks that must not do
 * VFS work themselves. No-op once the manager has been crowned.
 *
 * from_rename marks the packages.list write trigger, which bypasses the
 * attempt cap so a manager installed later can still be crowned.
 */
void ksu_request_manager_scan(bool from_rename);
#endif

#endif
