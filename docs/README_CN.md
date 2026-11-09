[English](README.md) | **简体中文** | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [Українська](README_UA.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | [日本語](README_JA.md) | [Español](README_ES.md)

---

<div align="center">
  <img src="/assets/kernelsu_next.png" width="96" alt="KernelSU Next Logo">

  <h2>KernelSU Next</h2>
  <p><strong>安卓设备基于内核的 Root 方案。</strong></p>

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

## 🚀 特性

- 基于内核的 `su` 和超级用户权限管理。
- 基于 [metamodules](https://kernelsu.org/guide/metamodule.html) 的模块系统：用于无系统修改的可插拔基础设施。
- GKI 2.0 设备支持内置 SUSFS LKM 驱动加载。
- 风险检测：阻止可疑模块被安装。
- 支持备份和恢复模块及应用配置文件。
- [应用配置文件](https://kernelsu.org/guide/app-profile.html)：把 Root 权限关进笼子里。

---

## ✅ 兼容性

KernelSU Next 支持从 **4.4 到 6.18** 的 Android 内核。

| 内核版本 | 支持说明 |
|----------------------|-------------------------------------------------------------------------|
| 5.10+ (GKI 2.0)      | 支持预构建镜像和 LKM/KMI |
| 4.19 – 5.4 (GKI 1.0) | 需要将 KernelSU 驱动编译到内核中 |
| < 4.14 (EOL)         | 需要 KernelSU 驱动（3.18+ 为实验性版本，可能需要回溯移植） |

**支持架构：** `arm64-v8a` 和 `x86_64`

> [!CAUTION]
> 较新的内核版本引入了破坏性变更，导致 KernelSU Next 在 `x86_64` 上运行失败，甚至可能触发 kernel panic！请前往网站获取更多信息。

---

## 📦 安装

请参考 [安装指南](https://kernelsu-next.github.io/webpage/pages/installation.html) 以获取设置说明。

---

## 🏅 贡献

- 前往 [Crowdin](https://crowdin.com/project/kernelsu-next) 为管理器提交翻译！
- 有关安全问题的报告，请参见 [SECURITY.md](/SECURITY.md)。

---

## 📜 许可证

- **`/kernel` 目录:** [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)。
- **其他所有文件:** [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html)。

---

## 💸 捐赠

如果您希望支持该项目：

- **USDT (BEP20, ERC20)**: `0x0b269716dc00692676e4217e23c7f67abd5f0f3e`
- **USDT (TRC20)**: `TXu6VX8dvJLQPx4WBobjbYT5tefHTDikWo`
- **LTC**: `LMYRqRXPKAFpYncBhNS44W1iwFit14LH1u`

---

## 🙏 致谢

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/) – 概念启发
- [Magisk](https://github.com/topjohnwu/Magisk) – 核心 root 实现
- [Genuine](https://github.com/brevent/genuine/) – APK v2 签名校验
- [Diamorphine](https://github.com/m0nad/Diamorphine) – rootkit 技术
- [KernelSU](https://github.com/tiann/KernelSU) – 让 KernelSU Next 成为可能的原始基础
- [Susfs4ksu](https://gitlab.com/simonpunk/susfs4ksu) – KernelSU 的内核级 root 隐藏扩展
- [Susfs4ksu-lkm](https://github.com/inforcqb/susfs4ksu-lkm) – 作为可加载内核模块移植的 SUSFS

