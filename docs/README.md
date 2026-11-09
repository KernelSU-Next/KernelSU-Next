**English** | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [Українська](README_UA.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | [日本語](README_JA.md) | [Español](README_ES.md)

---

<div align="center">
  <img src="/assets/kernelsu_next.png" width="96" alt="KernelSU Next Logo">

  <h2>KernelSU Next</h2>
  <p><strong>A kernel-based root solution for Android devices.</strong></p>

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

## 🚀 Features

- Kernel-based `su` and root access management.
- Module system based on [metamodules](https://kernelsu.org/guide/metamodule.html): Pluggable infrastructure for systemless modifications.
- Built-in SUSFS LKM driver loading for GKI 2.0 devices.
- Risk detection that blocks suspicious modules from installation.
- Backup and restore support for modules and app profiles.
- [App Profile](https://kernelsu.org/guide/app-profile.html): Lock up the root power in a cage.

---

## ✅ Compatibility

KernelSU Next supports Android kernels from **4.4 up to 6.18**.

| Kernel version       | Support notes                                                           |
|----------------------|-------------------------------------------------------------------------|
| 5.10+ (GKI 2.0)      | Supports pre-built images and LKM/KMI                                   |
| 4.19 – 5.4 (GKI 1.0) | Requires KernelSU driver built-in                                       |
| < 4.14 (EOL)         | Requires KernelSU driver (3.18+ is experimental and may need backports) |

**Supported architectures:** `arm64-v8a` and `x86_64`

> [!CAUTION]
> Recent kernel versions have implemented a breaking change causing KernelSU Next to fail and potentially trigger a kernel panic on `x86_64`! Check the website for more info!

---

## 📦 Installation

Please refer to the [Installation](https://kernelsu-next.github.io/webpage/pages/installation.html) guide for setup instructions.

---

## 🏅 Contribution

- Go to our [Crowdin](https://crowdin.com/project/kernelsu-next) to submit a translation for the manager!
- To report security issues, please see [SECURITY.md](/SECURITY.md).

---

## 📜 License

- **`/kernel` directory:** [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).
- **All other files:** [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html).

---

## 💸 Donations

If you'd like to support the project:

- **USDT (BEP20, ERC20)**: `0x0b269716dc00692676e4217e23c7f67abd5f0f3e`
- **USDT (TRC20)**: `TXu6VX8dvJLQPx4WBobjbYT5tefHTDikWo`
- **LTC**: `LMYRqRXPKAFpYncBhNS44W1iwFit14LH1u`

---

## 🙏 Credits

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/) – Concept inspiration
- [Magisk](https://github.com/topjohnwu/Magisk) – Core root implementation
- [Genuine](https://github.com/brevent/genuine/) – APK v2 signature validation
- [Diamorphine](https://github.com/m0nad/Diamorphine) – Rootkit techniques
- [KernelSU](https://github.com/tiann/KernelSU) – The original base that made KernelSU Next possible
- [Susfs4ksu](https://gitlab.com/simonpunk/susfs4ksu) – A kernel-level root-hiding extension for KernelSU
- [Susfs4ksu-lkm](https://github.com/inforcqb/susfs4ksu-lkm) – SUSFS ported as a loadable kernel module
