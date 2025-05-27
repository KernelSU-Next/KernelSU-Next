[English](README.md) | **简体中文** | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [Український](README_UA.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | [日本語](README_JA.md)

# KernelSU Next

<img src="/assets/kernelsu_next.png" style="width: 96px;" alt="logo">

安卓基于内核的 Root 方案

[![Latest Release](https://img.shields.io/github/v/release/KernelSU-Next/KernelSU-Next?label=Release&logo=github)](https://github.com/KernelSU-Next/KernelSU-Next/releases/latest)
[![Nightly Release](https://img.shields.io/badge/Nightly%20Release-gray?logo=hackthebox&logoColor=fff)](https://nightly.link/KernelSU-Next/KernelSU-Next/workflows/build-manager-ci/next/Manager)
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-orange.svg?logo=gnu)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![GitHub License](https://img.shields.io/github/license/KernelSU-Next/KernelSU-Next?logo=gnu)](/LICENSE)

## 特性

1. 基于内核的 `SU` 和权限管理
2. 基于动态挂载系统 [Magic Mount](https://topjohnwu.github.io/Magisk/details.html#magic-mount) / [OverlayFS](https://en.wikipedia.org/wiki/OverlayFS) 的模块系统。
3. [App Profile](https://kernelsu.org/zh_CN/guide/app-profile.html)：把 Root 权限关进笼子里

## 兼容状态

KernelSU Next 支持从 4.4 到 6.6 的大多数安卓内核
 - GKI 2.0（5.10+）内核可运行预置镜像和 LKM/KMI
 - GKI 1.0（4.19 - 5.4）内核需要使用 KernelSU 内核驱动重新编译
 - EOL (<4.14) 内核也需要使用 KernelSU 内核驱动重新编译 (3.18+ 的版本处于试验阶段，可能需要移植一些功能)

目前只支持 `arm64-v8a`, `armeabi-v7a` & `x86_64` 架构

## 用法

- [安装说明](https://ksunext.org/pages/installation.html)

## 安全性

有关报告 KernelSU Next 漏洞的信息，请参阅 [SECURITY.md](/SECURITY.md).

## 许可证

- 目录 `kernel` 下所有文件为 [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
- `kernel` 目录以外的其他部分均为 [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html)

## 鸣谢

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/): KernelSU 的灵感.
- [Magisk](https://github.com/topjohnwu/Magisk): 强大的 Root 工具.
- [genuine](https://github.com/brevent/genuine/): APK v2 签名验证。
- [Diamorphine](https://github.com/m0nad/Diamorphine): 一些 Rootkit 技巧。
- [KernelSU](https://github.com/tiann/KernelSU): 感谢 tiann，否则 KernelSU Next 根本不会存在。
- [Magic Mount Port](https://github.com/5ec1cff/KernelSU/blob/main/userspace/ksud/src/magic_mount.rs): 💜 5ec1cff 為了拯救 KernelSU！
