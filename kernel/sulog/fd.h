#ifndef __KSU_H_SULOG_FD
#define __KSU_H_SULOG_FD

#include <linux/init.h>

int ksu_install_sulog_fd(void);
void __init ksu_sulog_fd_init(void);
void ksu_sulog_fd_exit(void);

#endif