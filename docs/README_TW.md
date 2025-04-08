[English](README.md) | [简体中文](README_CN.md) | **繁體中文** | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | [Magyar](README_HU.md)

# KernelSU Next

<img src="/assets/kernelsu_next.png" style="width: 96px;" alt="logo">

基於內核的 Android 設備 Root 解決方案

[![Latest Release](https://img.shields.io/github/v/release/KernelSU-Next/KernelSU-Next?label=Release&logo=github)](https://github.com/KernelSU-Next/KernelSU-Next/releases/latest)
[![Nightly Release](https://img.shields.io/badge/Nightly%20Release-gray?logo=hackthebox&logoColor=fff)](https://nightly.link/KernelSU-Next/KernelSU-Next/workflows/build-manager-ci/next/Manager)
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-orange.svg?logo=gnu)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![GitHub License](https://img.shields.io/github/license/KernelSU-Next/KernelSU-Next?logo=gnu)](/LICENSE)

## 特性

1. 基於內核的 `su` 和 Root 權限管理
2. 基於動態掛載系統 [Magic Mount](https://topjohnwu.github.io/Magisk/details.html#magic-mount) / [OverlayFS](https://en.wikipedia.org/wiki/OverlayFS) 的模塊系統。
3. [App Profile](https://kernelsu.org/zh_CN/guide/app-profile.html)：把 Root 權限關進籠子裡

## 兼容狀態

KernelSU Next 正式支持大多數從 4.4 到 6.6 的 Android 內核
 - GKI 2.0 (5.10+) 內核可以運行預構建的映像和 LKM/KMI
 - GKI 1.0 (4.19 - 5.4) 內核需要重新編譯 KernelSU 驅動程序
 - EOL (<4.14) 內核也需要重新編譯 KernelSU 驅動程序（3.18+ 是實驗性的，可能需要移植一些功能）

目前僅支持 `arm64-v8a`

## 用法

- [安裝說明](https://KernelSU-Next.github.io/KernelSU-Next/)

## 安全性

有關報告 KernelSU Next 漏洞的信息，請參閱 [SECURITY.md](/SECURITY.md).

## 許可證

- 目錄 `kernel` 下所有文件為 [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
- `kernel` 目錄以外的其他部分均為 [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html)

## 鳴謝

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/): KernelSU 的靈感.
- [Magisk](https://github.com/topjohnwu/Magisk): 強大的 Root 工具.
- [genuine](https://github.com/brevent/genuine/): APK v2 簽名驗證。
- [Diamorphine](https://github.com/m0nad/Diamorphine): 一些 Rootkit 技巧。
- [KernelSU](https://github.com/tiann/KernelSU): 感謝 tiann，否則 KernelSU Next 根本不會存在。
- [Magic Mount Port](https://github.com/5ec1cff/KernelSU/blob/main/userspace/ksud/src/magic_mount.rs): 💜 5ec1cff 為了拯救 KernelSU！

