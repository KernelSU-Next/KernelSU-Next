#ifndef __KSU_H_KSU_SUSFS
#define __KSU_H_KSU_SUSFS

#include <linux/init.h>
#include <linux/types.h>

struct inode;

// Provided for fs/susfs.c: true when current task runs in the
// KernelSU SELinux domain
bool susfs_is_current_ksu_domain(void);

bool susfs_is_current_zygote_domain(void);

// devpts manual hook point expected by patched fs/devpts/inode.c
int ksu_handle_devpts(struct inode *inode);

// Initialize SIDs shared with the patched selinux avc code
void __init ksu_susfs_init_sids(void);

#endif
