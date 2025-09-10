#!/bin/bash

# This script builds the KernelSU Next manager APK.

# Ensure you have the setup Android SDK & NDK installed and necessary environment variables set and sourced.

# For LKM make sure you have imported the androidX-X.X_kernelsu.ko drivers to userspace/ksud_*/bin/aarch64 directory.

cross build --target aarch64-linux-android --release --manifest-path ./userspace/ksud_magic/Cargo.toml

cp userspace/ksud_magic/target/aarch64-linux-android/release/ksud manager/app/src/main/jniLibs/arm64-v8a/libksud_magic.so

cross build --target aarch64-linux-android --release --manifest-path ./userspace/ksud_overlayfs/Cargo.toml

cp userspace/ksud_overlayfs/target/aarch64-linux-android/release/ksud manager/app/src/main/jniLibs/arm64-v8a/libksud_overlayfs.so

cd userspace/susfsd/jni

ndk-build

cp ../libs/arm64-v8a/susfsd ../../../manager/app/src/main/jniLibs/arm64-v8a/libsusfsd.so

cd ../../..

cd manager

./setup.sh

cd ..

adb install manager/app/build/outputs/apk/release/KernelSU_Next_v*.apk
