[English](README.md) | **简体中文** | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [Український](README_UA.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | [日本語](README_JA.md)

---

<div align="center">
  <img src="/assets/kernelsu_next.png" width="96" alt="KernelSU Next Logo">
  
  <h2>KernelSU Next</h2>
  <p><strong>安卓设备基于内核的 Root 方案</strong></p>

  <p>
    <a href="https://github.com/KernelSU-Next/KernelSU-Next/releases/latest">
      <img src="https://img.shields.io/github/v/release/KernelSU-Next/KernelSU-Next?label=Release&logo=github" alt="Latest Release">
    </a>
    <a href="https://nightly.link/KernelSU-Next/KernelSU-Next/workflows/build-manager-ci/next/Manager">
      <img src="https://img.shields.io/badge/Nightly%20Release-gray?logo=hackthebox&logoColor=fff" alt="Nightly Build">
    </a>
    <a href="https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html">
      <img src="https://img.shields.io/badge/License-GPL%20v2-orange.svg?logo=gnu" alt="License: GPL v2">
    </a>
    <a href="/LICENSE">
      <img src="https://img.shields.io/github/license/KernelSU-Next/KernelSU-Next?logo=gnu" alt="GitHub License">
    </a>
  </p>
</div>

---

## 🚀 特性

- 基于内核的 `su` 和超级用户权限管理
- 动态挂载系统基于 **[Magic Mount](https://topjohnwu.github.io/Magisk/details.html#magic-mount)** 以及 **[OverlayFS](https://en.wikipedia.org/wiki/OverlayFS)**  
- [App Profile](https://kernelsu.org/zh_CN/guide/app-profile.html)：把 Root 权限关进笼子里

---

## ✅ 兼容性

KernelSU Next 支持从 **4.4 到 6.6** 的大多数安卓内核

| 内核版本 | 支持情况 |
|----------------|---------------|
| 5.10+ (GKI 2.0) | 可运行预置镜像和 LKM/KMI |
| 4.19 – 5.4 (GKI 1.0) | 需要使用 KernelSU 内核驱动重新编译 |
| <4.14 (EOL) | 需要使用 KernelSU 内核驱动重新编译（3.18+ 的版本处于试验阶段，可能需要进行回溯移植） |

**支持的架构：**  
`arm64-v8a`、`armeabi-v7a`、`x86_64`

---

## 📦 安装

请遵循该[安装说明](https://kernelsu-next.github.io/webpage/zh_CN/pages/installation.html)进行操作。

---

## 🔐 安全性

有关报告 KernelSU Next 漏洞的信息，请参阅 [SECURITY.md](/SECURITY.md).

---

## 📜 许可

- **目录 `/kernel` 下所有文件**为 [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
- **`/kernel` 目录以外的其他部分**均为 [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html)

---

## 💸 捐赠

如果你喜欢这个项目还请支持：

- **USDT (BEP20, ERC20)**: `0x12b5224b7aca0121c2f003240a901e1d064371c1`  
- **USDT (TRC20)**: `TYUVMWGTcnR5svnDoX85DWHyqUAeyQcdjh`  
- **ETH (ERC20)**: `0x12b5224b7aca0121c2f003240a901e1d064371c1`  
- **LTC**: `Ld238uYBuRQdZB5YwdbkuU6ektBAAUByoL`  
- **BTC**: `19QgifcjMjSr1wB2DJcea5cxitvWVcXMT6`

---

## 🙏 鸣谢

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/)：KernelSU 的灵感.
- [Magisk](https://github.com/topjohnwu/Magisk)：强大的 Root 工具.
- [genuine](https://github.com/brevent/genuine/)：APK v2 签名验证。
- [Diamorphine](https://github.com/m0nad/Diamorphine)：一些 Rootkit 技巧。
- [KernelSU](https://github.com/tiann/KernelSU)：感谢 tiann，否则 KernelSU Next 根本不会存在。
- [Magic Mount Port](https://github.com/5ec1cff/KernelSU/blob/main/userspace/ksud/src/magic_mount.rs)：💜 5ec1cff 为了拯救 KernelSU！
