#ifndef __KSU_H_SELINUX_HIDE
#define __KSU_H_SELINUX_HIDE

#include <linux/types.h>

void __init ksu_selinux_hide_init();
void __exit ksu_selinux_hide_exit();

#endif