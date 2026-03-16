[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [Українська](README_UA.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | [日本語](README_JA.md) | [Español](README_ES.md) | **ភាសាខ្មែរ**

---

<div align="center">
  <img src="/assets/kernelsu_next.png" width="96" alt="KernelSU Next Logo">

  <h2>KernelSU Next</h2>
  <p><strong>ដំណោះស្រាយ Root ដែលផ្អែកលើខឺណែលសម្រាប់ឧបករណ៍ Android។</strong></p>

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

## 🚀 លក្ខណៈពិសេស

- ការគ្រប់គ្រងការចូលប្រើ root និង `su` ដោយផ្អែកលើខឺណែល។
- ប្រព័ន្ធម៉ូឌុលដោយផ្អែកលើ [Magic Mount](https://topjohnwu.github.io/Magisk/details.html#magic-mount) និង [OverlayFS](https://en.wikipedia.org/wiki/OverlayFS)។
- [App Profile](https://kernelsu.org/guide/app-profile.html)៖ ដាក់កម្រិតសិទ្ធិ root សម្រាប់កម្មវិធីនីមួយៗ។

---

## ✅ ភាពត្រូវគ្នា

KernelSU Next គាំទ្រខឺណែល Android ពីជំនាន់ **4.4 ដល់ 6.6**។

| ជំនាន់ខឺណែល            | កំណត់ចំណាំការគាំទ្រ                                                                                  |
|----------------------|---------------------------------------------------------------------------------------------------|
| 5.10+ (GKI 2.0)      | គាំទ្ររូបភាពដែលបានបង្កើតជាមុន និង LKM/KMI                                                              |
| 4.19 – 5.4 (GKI 1.0) | តម្រូវឲ្យមាន driver KernelSU បញ្ចូលមកស្រាប់                                                          |
| < 4.14 (EOL)         | តម្រូវឲ្យមាន driver KernelSU (ជំនាន់ 3.18+ គឺស្ថិតក្នុងដំណាក់កាលសាកល្បង ហើយអាចត្រូវការការកែសម្រួលបន្ថែម) |

**ស្ថាបត្យកម្មដែលបានគាំទ្រ៖** `arm64-v8a`, `armeabi-v7a` និង `x86_64`

---

## 📦 ការដំឡើង

សូមមើលមគ្គុទ្ទេសក៍ [ការដំឡើង](https://kernelsu-next.github.io/webpage/pages/installation.html) សម្រាប់ការណែនាំអំពីការរៀបចំ។

---

## 🏅 ការចូលរួមចំណែក

- ចូលទៅកាន់ [Crowdin](https://crowdin.com/project/kernelsu-next) របស់យើងដើម្បីដាក់បញ្ចូលការបកប្រែសម្រាប់កម្មវិធីគ្រប់គ្រង!
- ដើម្បីរាយការណ៍ពីបញ្ហាសុវត្ថិភាព សូមមើលឯកសារ [SECURITY.md](/SECURITY.md)។

---

## 📜 អាជ្ញាប័ណ្ណ

- **ថតឯកសារ `/kernel`៖** [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)។
- **ឯកសារផ្សេងទៀតទាំងអស់៖** [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html)។

---

## 💸 ការបរិច្ចាគ

ប្រសិនបើអ្នកចង់គាំទ្រគម្រោងនេះ៖

- **USDT (BEP20, ERC20)**: `0x12b5224b7aca0121c2f003240a901e1d064371c1`
- **USDT (TRC20)**: `TYUVMWGTcnR5svnDoX85DWHyqUAeyQcdjh`
- **USDT (SOL)**: `A4wqBXYd6Ey4nK4SJ2bmjeMgGyaLKT9TwDLh8BEo8Zu6`
- **ETH (ERC20)**: `0x12b5224b7aca0121c2f003240a901e1d064371c1`
- **LTC**: `Ld238uYBuRQdZB5YwdbkuU6ektBAAUByoL`
- **BTC**: `19QgifcjMjSr1wB2DJcea5cxitvWVcXMT6`

---

## 🙏 ថ្លែងអំណរគុណ

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/) – បំផុសគំនិត
- [Magisk](https://github.com/topjohnwu/Magisk) – ការអនុវត្ត root ចម្បង
- [Genuine](https://github.com/brevent/genuine/) – ការផ្ទៀងផ្ទាត់ហត្ថលេខា APK v2
- [Diamorphine](https://github.com/m0nad/Diamorphine) – បច្ចេកទេស Rootkit
- [KernelSU](https://github.com/tiann/KernelSU) – មូលដ្ឋានដើមដែលធ្វើឲ្យ KernelSU Next អាចបង្កើតបាន
- [Magic Mount Port](https://github.com/5ec1cff/KernelSU/blob/main/userspace/ksud/src/magic_mount.rs) – សម្រាប់ការគាំទ្រ Magic Mount
