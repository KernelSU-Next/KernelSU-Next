[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | **Русский** | [Українська](README_UA.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | [日本語](README_JA.md)

---

<div align="center">
  <img src="/assets/kernelsu_next.png" width="96" alt="KernelSU Next Logo">

  <h2>KernelSU Next</h2>
  <p><strong>Решение root на базе ядра для устройств Android.</strong></p>

  <p>
    <a href="https://github.com/KernelSU-Next/KernelSU-Next/releases/latest">
      <img src="https://img.shields.io/github/v/release/KernelSU-Next/KernelSU-Next?label=Release&logo=github" alt="Последний релиз">
    </a>
    <a href="https://nightly.link/KernelSU-Next/KernelSU-Next/workflows/build-manager-ci/next/Manager">
      <img src="https://img.shields.io/badge/Nightly%20Release-gray?logo=hackthebox&logoColor=fff" alt="Ночная сборка">
    </a>
    <a href="https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html">
      <img src="https://img.shields.io/badge/License-GPL%20v2-orange.svg?logo=gnu" alt="Лицензия: GPL v2">
    </a>
    <a href="/LICENSE">
      <img src="https://img.shields.io/github/license/KernelSU-Next/KernelSU-Next?logo=gnu" alt="Лицензия GitHub">
    </a>
  </p>
</div>

---

## 🚀 Функции

- Управление `su` и root-доступом на уровне ядра.
- Система модулей на основе [Magic Mount](https://topjohnwu.github.io/Magisk/details.html#magic-mount) и [OverlayFS](https://en.wikipedia.org/wiki/OverlayFS).
- [Профили Приложений](https://kernelsu.org/guide/app-profile.html): ограничение root-прав для отдельных приложений.

---

## ✅ Совместимость

KernelSU Next поддерживает ядра Android версий **от 4.4 до 6.6**:

| Версия ядра          | Примечания по поддержке                                                                 |
|----------------------|-----------------------------------------------------------------------------------------|
| 5.10+ (GKI 2.0)      | Поддержка предсобранных образов и LKM/KMI                                               |
| 4.19 – 5.4 (GKI 1.0) | Требуется встроенный драйвер KernelSU                                                   |
| < 4.14 (EOL)         | Требуется драйвер KernelSU (3.18+ экспериментальная поддержка, может потребоваться порт) |

**Поддерживаемые архитектуры:** `arm64-v8a`, `armeabi-v7a`, `x86_64`

---

## 📦 Установка

Пожалуйста, ознакомьтесь с [руководством по установке](https://kernelsu-next.github.io/webpage/pages/installation.html) для получения инструкций по настройке.

---

## 🔐 Безопасность

Для сообщений о проблемах безопасности см. [SECURITY.md](/SECURITY.md).

---

## 📜 Лицензия

- **Каталог `/kernel`:** [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).
- **Все остальные файлы:** [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html).

---

## 💸 Пожертвования

Если хотите поддержать проект:

- **USDT (BEP20, ERC20)**: `0x12b5224b7aca0121c2f003240a901e1d064371c1`
- **USDT (TRC20)**: `TYUVMWGTcnR5svnDoX85DWHyqUAeyQcdjh`
- **ETH (ERC20)**: `0x12b5224b7aca0121c2f003240a901e1d064371c1`
- **LTC**: `Ld238uYBuRQdZB5YwdbkuU6ektBAAUByoL`
- **BTC**: `19QgifcjMjSr1wB2DJcea5cxitvWVcXMT6`

---

## 🙏 Благодарности

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/) – вдохновение для концепции
- [Magisk](https://github.com/topjohnwu/Magisk) – основная реализация root
- [Genuine](https://github.com/brevent/genuine/) – проверка подписи APK v2
- [Diamorphine](https://github.com/m0nad/Diamorphine) – техники RootKit
- [KernelSU](https://github.com/tiann/KernelSU) – основа для создания KernelSU Next
- [Magic Mount Port](https://github.com/5ec1cff/KernelSU/blob/main/userspace/ksud/src/magic_mount.rs) – 💜 5ec1cff за сохранение KernelSU
