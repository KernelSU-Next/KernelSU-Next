#include <jni.h>
#include <errno.h>
#include <sys/prctl.h>

extern "C" JNIEXPORT jlong JNICALL
Java_com_rifsxd_ksunext_Natives_getLegacyInfo(JNIEnv *env, jclass /* clazz */) {
    int32_t version = -1;
    int32_t flags = 0;
    int32_t result = 0;

    errno = 0;
    prctl(0xDEADBEEF, 2, &version, &flags, &result);

    if (errno == EINVAL || version == -1) {
        return -1L;
    }

    return (static_cast<jlong>(version) << 32) | (static_cast<jlong>(flags) & 0xFFFFFFFFL);
}