[English](README.md) | [简体中文](README_CN.md) | **繁體中文** | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [Українська](README_UA.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | [日本語](README_JA.md) | [Español](README_ES.md)

---

<div align="center">
  <img src="/assets/kernelsu_next.png" width="96" alt="KernelSU Next Logo">

  <h2>KernelSU Next</h2>
  <p><strong>基於內核的 Android 裝置 Root 解決方案。</strong></p>

  <p>
    <a href="https://github.com/KernelSU-Next/KernelSU-Next/releases/latest">
      <img src="https://img.shields.io/github/v/release/KernelSU-Next/KernelSU-Next?label=Release&logo=github" alt="Latest Release">
    </a>
    <a href="https://github.com/KernelSU-Next/KernelSU-Next/releases">
      <img src="https://img.shields.io/github/downloads/KernelSU-Next/KernelSU-Next/total?label=Downloads&logo=github" alt="Total Downloads">
    </a>
    <a href="https://nightly.link/KernelSU-Next/KernelSU-Next/workflows/build-manager-ci/dev/manager">
      <img src="https://img.shields.io/badge/Nightly%20Release-gray?logo=hackthebox&logoColor=fff" alt="Nightly Build">
    </a>
    <a href="https://github.com/KernelSU-Next/KernelSU-Next/actions/workflows/build-manager-ci.yml">
      <img src="https://github.com/KernelSU-Next/KernelSU-Next/actions/workflows/build-manager-ci.yml/badge.svg" alt="Build Status">
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

## 🚀 功能

- 基於內核的 `su` 和 root 權限管理。
- 基於 [metamodules](https://kernelsu.org/guide/metamodule.html) 的模組系統：用於系統級修改的可插拔基礎架構。
- GKI 2.0 裝置支援內建 SUSFS LKM 驅動載入。
- 風險偵測：阻止可疑模組被安裝。
- 支援備份與還原模組與應用程式設定檔。
- [應用程式設定檔](https://kernelsu.org/guide/app-profile.html)：將 root 權限限制在隔離的容器內。

---

## ✅ 相容性

KernelSU Next 支援 Android 從 **4.4 到 6.18** 的內核版本。

| 內核版本 | 支援資訊 |
|----------------------|-------------------------------------------------------------------------|
| 5.10+ (GKI 2.0)      | 支援預先編譯映像與 LKM/KMI |
| 4.19 – 5.4 (GKI 1.0) | 需要將 KernelSU 驅動程式編譯進核心 |
| < 4.14 (EOL)         | 需要 KernelSU 驅動程式（3.18+ 為實驗性版本，可能需要回溯移植） |

**支援的架構：** `arm64-v8a` 和 `x86_64`

> [!CAUTION]
> 較新的內核版本已實施破壞性修改，導致 KernelSU Next 在 `x86_64` 上失敗，甚至可能觸發 kernel panic！請查看官方網站以了解更多資訊。

---

## 📦 安裝

請參閱 [安裝指南](https://kernelsu-next.github.io/webpage/pages/installation.html) 以完成安裝。

---

## 🏅 貢獻

- 前往 [Crowdin](https://crowdin.com/project/kernelsu-next) 為管理器提交翻譯！
- 如需回報安全問題，請參閱 [SECURITY.md](/SECURITY.md)。

---

## 📜 授權

- **`/kernel` 目錄:** [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)。
- **其他所有檔案:** [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html)。

---

## 💸 捐贈

如果您想支持這個專案：

- **USDT (BEP20, ERC20)**: `0x0b269716dc00692676e4217e23c7f67abd5f0f3e`
- **USDT (TRC20)**: `TXu6VX8dvJLQPx4WBobjbYT5tefHTDikWo`
- **LTC**: `LMYRqRXPKAFpYncBhNS44W1iwFit14LH1u`

---

## 🙏 致謝

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/) – 概念靈感
- [Magisk](https://github.com/topjohnwu/Magisk) – 核心 root 實作
- [Genuine](https://github.com/brevent/genuine/) – APK v2 簽章驗證
- [Diamorphine](https://github.com/m0nad/Diamorphine) – rootkit 技術
- [KernelSU](https://github.com/tiann/KernelSU) – KernelSU Next 得以誕生的原始基礎
- [Susfs4ksu](https://gitlab.com/simonpunk/susfs4ksu) – KernelSU 的核心級 root 隱藏擴充
- [Susfs4ksu-lkm](https://github.com/inforcqb/susfs4ksu-lkm) – 以可載入核心模組形式移植的 SUSFS

