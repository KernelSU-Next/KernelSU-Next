#ifndef __KSU_UAPI_FEATURE_H
#define __KSU_UAPI_FEATURE_H

enum ksu_feature_id {
    KSU_FEATURE_SU_COMPAT = 0,
    KSU_FEATURE_KERNEL_UMOUNT = 1,

    // custom extensions
    KSU_FEATURE_AVC_SPOOF = 10003,

    KSU_FEATURE_MAX
};

#endif
