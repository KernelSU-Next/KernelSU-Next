#ifndef __KSU_H_APP_PROFILE
#define __KSU_H_APP_PROFILE

#include "uapi/app_profile.h" // IWYU pragma: keep

// Escalate current process to root with the appropriate profile
int escape_with_root_profile(void);

void escape_to_root_for_init(void);

#endif
