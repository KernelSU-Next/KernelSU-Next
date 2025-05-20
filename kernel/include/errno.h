#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)
#include <uapi/asm-generic/errno-base.h> // For kernels 3.7 and newer
#else
#include <asm-generic/errno-base.h> // For kernels older than 3.7
#endif
