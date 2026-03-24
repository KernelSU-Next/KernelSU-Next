[English](README.md) | [简体中文](README_CN.md) | **繁體中文** | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [Українська](README_UA.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | [日本語](README_JA.md) | [Español](README_ES.md)

---

<div align="center">
  <img src="/assets/kernelsu_next.png" width="96" alt="KernelSU Next Logo">

  <h2>KernelSU Next</h2>
  <p><strong>基於內核的 Android 設備 Root 解決方案。</strong></p>

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
    <a title="Crowdin" target="_blank" href="https://crowdin.com/project/kernelsu-next"><img src="https://badges.crowdin.net/kernelsu-next/localized.svg"></a>
  </p>
</div>

---

## 🚀 特性

- 基於內核的 `su` 和 Root 權限管理。
- 模塊系統基於 [Magic Mount](https://topjohnwu.github.io/Magisk/details.html#magic-mount) 以及 [OverlayFS](https://en.wikipedia.org/wiki/OverlayFS)。
- [App Profile](https://kernelsu.org/zh_TW/guide/app-profile.html)：把 Root 權限關進籠子裡。

---

## ✅ 兼容狀態

KernelSU Next 正式支持大多數從 **4.4 到 6.6** 的 Android 內核。

| 内核版本              | 支援狀況                                                               |
|----------------------|------------------------------------------------------------------------|
| 5.10+ (GKI 2.0)      | 可以運行預構建的映像和 LKM/KMI                                           |
| 4.19 – 5.4 (GKI 1.0) | 需要重新編譯 KernelSU 驅動程序                                           |
| < 4.14 (EOL)         | 需要重新編譯 KernelSU 驅動程序（3.18+ 是實驗性的，可能需要回溯移植一些功能） |

**支援的架構：** `arm64-v8a`、`armeabi-v7a`、`x86_64`

> [!CAUTION]
> 最近的核心版本引入了一項破壞性更改，導致 KernelSU Next 在 `x86_64` 上執行失敗，甚至可能引發核心恐慌 (kernel panic)！請查看網站獲取更多資訊！

---

## 📦 用法

請遵循[安裝説明](https://kernelsu-next.github.io/webpage/pages/installation.html)進行操作。

---

## 🏅 貢獻

- 前往我們的 [Crowdin](https://crowdin.com/project/kernelsu-next) 為管理器提交翻譯！
- 有關報告 KernelSU Next 漏洞的信息，請參閱 [SECURITY.md](/SECURITY.md)。

---

## 📜 許可證

- **目錄 `/kernel` 下所有文件**為 [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)。
- **`/kernel` 目錄以外的其他部分**均為 [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html)。

---

## 💸 抖内

- **USDT (BEP20, ERC20)**: `0x12b5224b7aca0121c2f003240a901e1d064371c1`
- **USDT (TRC20)**: `TYUVMWGTcnR5svnDoX85DWHyqUAeyQcdjh`
- **USDT (SOL)**: `A4wqBXYd6Ey4nK4SJ2bmjeMgGyaLKT9TwDLh8BEo8Zu6`
- **ETH (ERC20)**: `0x12b5224b7aca0121c2f003240a901e1d064371c1`
- **LTC**: `Ld238uYBuRQdZB5YwdbkuU6ektBAAUByoL`
- **BTC**: `19QgifcjMjSr1wB2DJcea5cxitvWVcXMT6`

---

## 🙏 鳴謝

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/)：KernelSU 的靈感。
- [Magisk](https://github.com/topjohnwu/Magisk)：強大的 Root 工具。
- [Genuine](https://github.com/brevent/genuine/)：APK v2 簽名驗證。
- [Diamorphine](https://github.com/m0nad/Diamorphine)：一些 Rootkit 技巧。
- [KernelSU](https://github.com/tiann/KernelSU)：感謝 tiann，否則 KernelSU Next 根本不會存在。
- [Magic Mount Port](https://github.com/5ec1cff/KernelSU/blob/main/userspace/ksud/src/magic_mount.rs)：💜 5ec1cff 為了拯救 KernelSU！
