[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | **Русский** | [Українська](README_UA.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | [日本語](README_JA.md) | [Español](README_ES.md)

---

<div align="center">
  <img src="/assets/kernelsu_next.png" width="96" alt="KernelSU Next Logo">

  <h2>KernelSU Next</h2>
  <p><strong>Решение для root на базе ядра для устройств Android.</strong></p>

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

## 🚀 Возможности

- Управление доступом root и `su` на основе ядра.
- Система модулей на базе [metamodules](https://kernelsu.org/guide/metamodule.html): подключаемая инфраструктура для системных модификаций.
- Встроенная загрузка драйвера SUSFS LKM для устройств GKI 2.0.
- Обнаружение рисков, блокирующее установку подозрительных модулей.
- Поддержка резервного копирования и восстановления модулей и профилей приложений.
- [Профиль приложения](https://kernelsu.org/guide/app-profile.html): ограничивает право root в безопасной клетке.

---

## ✅ Совместимость

KernelSU Next поддерживает ядра Android с **4.4 до 6.18**.

| Версия ядра | Примечания по поддержке |
|----------------------|-------------------------------------------------------------------------|
| 5.10+ (GKI 2.0)      | Поддержка предварительно собранных образов и LKM/KMI |
| 4.19 – 5.4 (GKI 1.0) | Требуется встроенный драйвер KernelSU в ядро |
| < 4.14 (EOL)         | Требуется драйвер KernelSU (3.18+ экспериментальный и может потребовать бэкпорты) |

**Поддерживаемые архитектуры:** `arm64-v8a` и `x86_64`

> [!CAUTION]
> Новые версии ядра внесли критическое изменение, из-за которого KernelSU Next может перестать работать и даже вызвать kernel panic на `x86_64`! Проверьте сайт для получения дополнительной информации!

---

## 📦 Установка

Пожалуйста, обратитесь к руководству по [установке](https://kernelsu-next.github.io/webpage/pages/installation.html) для получения инструкций.

---

## 🏅 Вклад

- Перейдите на [Crowdin](https://crowdin.com/project/kernelsu-next), чтобы отправить перевод для менеджера!
- Чтобы сообщить о проблемах безопасности, см. [SECURITY.md](/SECURITY.md).

---

## 📜 Лицензия

- **каталог `/kernel`:** [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).
- **Все остальные файлы:** [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html).

---

## 💸 Пожертвования

Если вы хотите поддержать проект:

- **USDT (BEP20, ERC20)**: `0x0b269716dc00692676e4217e23c7f67abd5f0f3e`
- **USDT (TRC20)**: `TXu6VX8dvJLQPx4WBobjbYT5tefHTDikWo`
- **LTC**: `LMYRqRXPKAFpYncBhNS44W1iwFit14LH1u`

---

## 🙏 Благодарности

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/) – Идеи и концепция
- [Magisk](https://github.com/topjohnwu/Magisk) – Основная реализация root
- [Genuine](https://github.com/brevent/genuine/) – Проверка подписи APK v2
- [Diamorphine](https://github.com/m0nad/Diamorphine) – Техники rootkit
- [KernelSU](https://github.com/tiann/KernelSU) – Исходная база, благодаря которой стал возможен KernelSU Next
- [Susfs4ksu](https://gitlab.com/simonpunk/susfs4ksu) – Расширение скрытия root уровня ядра для KernelSU
- [Susfs4ksu-lkm](https://github.com/inforcqb/susfs4ksu-lkm) – SUSFS, портированный как загружаемый модуль ядра

