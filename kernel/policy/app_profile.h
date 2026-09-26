#ifndef __KSU_H_APP_PROFILE
#define __KSU_H_APP_PROFILE

#include "uapi/app_profile.h"
#include "linux/init.h"

#define TIF_KSU_DISABLE_ESCAPE_WITH_ROOT 63

// Escalate current process to root with the appropriate profile
int escape_with_root_profile(void);

// Drop the current task's seccomp filter (no kernel equivalent on < 5.10)
void disable_seccomp(void);

void escape_to_root_for_init(void);

void __init ksu_app_profile_init(void);

#endif