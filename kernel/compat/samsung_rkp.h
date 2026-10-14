#ifndef __KSU_SAMSUNG_RKP_H
#define __KSU_SAMSUNG_RKP_H

#include <linux/types.h>

int ksu_samsung_rkp_init(void);
void ksu_samsung_rkp_exit(void);
bool ksu_samsung_device_detected(void);

#endif
