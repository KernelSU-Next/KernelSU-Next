#ifndef __KSU_H_SYSCALL_TABLE_HOOK
#define __KSU_H_SYSCALL_TABLE_HOOK

// Syscall table patching hook scheme: directly patch sys_call_table entries
// with wrapper functions instead of using kprobes/tracepoints.
// Intended for non-GKI kernels where kprobes are unavailable or broken.

void __init ksu_syscall_table_hook_init(void);
void __exit ksu_syscall_table_hook_exit(void);

#endif
