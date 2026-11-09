[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [Українська](README_UA.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | **Български** | [日本語](README_JA.md) | [Español](README_ES.md)

---

<div align="center">
  <img src="/assets/kernelsu_next.png" width="96" alt="KernelSU Next Logo">

  <h2>KernelSU Next</h2>
  <p><strong>Решение за root, базирано на ядро, за Android устройства.</strong></p>

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

## 🚀 Функции

- Управление на root достъпа и `su` на базата на ядро.
- Система от модули, базирана на [metamodules](https://kernelsu.org/guide/metamodule.html): приставима инфраструктура за системни модификации.
- Вградено зареждане на SUSFS LKM драйвер за устройства GKI 2.0.
- Откриване на рискове, което блокира инсталирането на подозрителни модули.
- Поддръжка за архивиране и възстановяване на модули и профили на приложения.
- [Профил на приложение](https://kernelsu.org/guide/app-profile.html): затваря силата на root в клетка.

---

## ✅ Съвместимост

KernelSU Next поддържа Android ядра от **4.4 до 6.18**.

| Версия на ядро | Бележки за поддръжка |
|----------------------|-------------------------------------------------------------------------|
| 5.10+ (GKI 2.0)      | Поддържа предварително компилирани изображения и LKM/KMI |
| 4.19 – 5.4 (GKI 1.0) | Изисква драйверът на KernelSU да бъде вградена в ядрото |
| < 4.14 (EOL)         | Изисква драйвер на KernelSU (3.18+ е експериментален и може да изисква backports) |

**Поддържани архитектури:** `arm64-v8a` и `x86_64`

> [!CAUTION]
> Съвременните версии на ядрото въведоха критична промяна, която кара KernelSU Next да се срине и може да предизвика kernel panic на `x86_64`! Проверете уебсайта за повече информация!

---

## 📦 Инсталация

Моля, вижте [Инструкции за инсталиране](https://kernelsu-next.github.io/webpage/pages/installation.html) за настройки.

---

## 🏅 Принос

- Отидете в [Crowdin](https://crowdin.com/project/kernelsu-next), за да изпратите превод за мениджъра!
- За докладване на проблеми със сигурността вижте [SECURITY.md](/SECURITY.md).

---

## 📜 Лиценз

- **директория `/kernel`:** [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).
- **Всички останали файлове:** [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html).

---

## 💸 Дарения

Ако искате да подкрепите проекта:

- **USDT (BEP20, ERC20)**: `0x0b269716dc00692676e4217e23c7f67abd5f0f3e`
- **USDT (TRC20)**: `TXu6VX8dvJLQPx4WBobjbYT5tefHTDikWo`
- **LTC**: `LMYRqRXPKAFpYncBhNS44W1iwFit14LH1u`

---

## 🙏 Кредити

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/) – Концептуална вдъхновяваща идея
- [Magisk](https://github.com/topjohnwu/Magisk) – Основна реализация на root
- [Genuine](https://github.com/brevent/genuine/) – Проверка на APK v2 подпис
- [Diamorphine](https://github.com/m0nad/Diamorphine) – Техники на rootkit
- [KernelSU](https://github.com/tiann/KernelSU) – Оригиналната база, която направи KernelSU Next възможен
- [Susfs4ksu](https://gitlab.com/simonpunk/susfs4ksu) – Разширение за скриване на root на ниво ядро за KernelSU
- [Susfs4ksu-lkm](https://github.com/inforcqb/susfs4ksu-lkm) – SUSFS, портирован като зареждаем модул на ядро

