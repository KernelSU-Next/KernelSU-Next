#ifndef __KSU_H_ADB_ROOT
#define __KSU_H_ADB_ROOT
#include <asm/ptrace.h>

#ifdef CONFIG_KSU_SUSFS
long ksu_adb_root_handle_execveat(const char *filename, void __user ***envp_user_ptr);
#else
long ksu_adb_root_handle_execve(struct pt_regs *regs);
long ksu_adb_root_handle_execveat(struct pt_regs *regs);
#endif

void ksu_adb_root_init(void);

void ksu_adb_root_exit(void);

#endif
