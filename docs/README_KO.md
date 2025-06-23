[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | **한국어** | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [Український](README_UA.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | [日本語](README_JA.md)

# KernelSU Next

<img src="/assets/kernelsu_next.png" style="width: 96px;" alt="logo">

안드로이드 기기들을 위한 커널 기반 루팅 솔루션입니다.

[![Latest Release](https://img.shields.io/github/v/release/KernelSU-Next/KernelSU-Next?label=Release&logo=github)](https://github.com/KernelSU-Next/KernelSU-Next/releases/latest)
[![Nightly Release](https://img.shields.io/badge/Nightly%20Release-gray?logo=hackthebox&logoColor=fff)](https://nightly.link/KernelSU-Next/KernelSU-Next/workflows/build-manager-ci/next/Manager)
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-orange.svg?logo=gnu)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![GitHub License](https://img.shields.io/github/license/KernelSU-Next/KernelSU-Next?logo=gnu)](/LICENSE)

## 기능

1. 커널 기반 `su` 및 루트 권한 관리
2. 동적 마운트 시스템 기반 모듈 시스템 [Magic Mount](https://topjohnwu.github.io/Magisk/details.html#magic-mount) / [OverlayFS](https://en.wikipedia.org/wiki/OverlayFS).
3. [App Profile](https://kernelsu.org/guide/app-profile.html): 루트 권한 제한

## 호환 상태

KernelSU Next는 공식적으로 대부분의 4.4부터 6.6의 안드로이드 커널을 지원합니다.
 - GKI 2.0 (5.10+) 커널은 미리 빌드된 이미지와 LKM/KMI를 지원합니다.
 - GKI 1.0 (4.19 - 5.4) 커널은 KernelSU 드라이버로 다시 빌드해야 합니다.
 - EOL (<4.14) 커널도 역시 KernelSU 드라이버로 다시 빌드해야 합니다.(3.18+는 실험적이며 일부 함수의 이식이 필요할 수 있습니다.).

현재는, `arm64-v8a`, `armeabi-v7a` & `x86_64` 만 지원됩니다.

## 사용 방법

- [설치 방법](https://ksunext.org/pages/installation.html)

## 보안

KernelSU의 보안 취약점 보고에 대한 자세한 내용은 [SECURITY.md](/SECURITY.md)를 참조하세요.

## 저작권 라이센스

- `kernel` 디렉터리의 파일은 [GPL-2.0전용](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)입니다.
- `kernel` 디렉터리를 제외한 모든 파일은 [GPL-3.0-이상](https://www.gnu.org/licenses/gpl-3.0.html)입니다.

## 크레딧

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/): KernelSU의 아이디어
- [Magisk](https://github.com/topjohnwu/Magisk): 강력한 루팅 도구
- [genuine](https://github.com/brevent/genuine/): APK v2 서명 검사
- [Diamorphine](https://github.com/m0nad/Diamorphine): 일부 rootkit 기술
- [KernelSU](https://github.com/tiann/KernelSU): KernelSU Next가 존재할 수 있게 해 준 tiann에게 감사드립니다.
- [Magic Mount Port](https://github.com/5ec1cff/KernelSU/blob/main/userspace/ksud/src/magic_mount.rs): KernelSU를 구해준 5ec1cff에게 감사드립니다!
