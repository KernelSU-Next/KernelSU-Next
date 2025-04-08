[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | **Русский** | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | [Magyar](README_HU.md)

# KernelSU Next

<img src="/assets/kernelsu_next.png" style="width: 96px;" alt="logo">

Root-решение для Android на базе ядра. 

[![Latest Release](https://img.shields.io/github/v/release/KernelSU-Next/KernelSU-Next?label=Release&logo=github)](https://github.com/KernelSU-Next/KernelSU-Next/releases/latest)
[![Nightly Release](https://img.shields.io/badge/Nightly%20Release-gray?logo=hackthebox&logoColor=fff)](https://nightly.link/KernelSU-Next/KernelSU-Next/workflows/build-manager-ci/next/Manager)
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-orange.svg?logo=gnu)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![GitHub License](https://img.shields.io/github/license/KernelSU-Next/KernelSU-Next?logo=gnu)](/LICENSE)

## Функции

1. Реализация `su` и управление root-доступом прямо на уровне ядра.  
2. Динамическая система модулей, построенная на [Magic Mount](https://topjohnwu.github.io/Magisk/details.html#magic-mount) / [OverlayFS](https://en.wikipedia.org/wiki/OverlayFS).  
3. [Профиль для приложений](https://kernelsu.org/guide/app-profile.html): позволяет ограничить root-доступ в песочницу для отдельных приложений.

## Совместимость

KernelSU Next работает с большинством ядер Android (4.4 - 6.6):  
- GKI 2.0 (5.10+) могут использовать предсобранные образы и LKM/KMI.  
- GKI 1.0 (4.19 - 5.4) требуют пересборки с драйвером KernelSU.  
- EOL (<4.14) также требуют пересборки с драйвером KernelSU (версии 3.18+ экспериментальные и могут потребовать некоторые функции бэкпортов).  

Сейчас поддерживается только `arm64-v8a`.

## Использование

- [Инструкция по установке](https://KernelSU-Next.github.io/KernelSU-Next/)

## Безопасность

Если нашли баг, посмотри [SECURITY.md](/SECURITY.md) — там гайд, как сообщить о проблеме.

## Лицензия

- Всё, что в директории `kernel`, — [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).  
- Остальной код, кроме директории `kernel`, под [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html).

## Донаты

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ USDT BEP20 ]

- TYUVMWGTcnR5svnDoX85DWHyqUAeyQcdjh [ USDT TRC20 ]

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ USDT ERC20 ]

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ ETH ERC20 ]

- Ld238uYBuRQdZB5YwdbkuU6ektBAAUByoL [ LTC ]

- 19QgifcjMjSr1wB2DJcea5cxitvWVcXMT6 [ BTC ]

## Благодарность

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/): Идея KernelSU.  
- [Magisk](https://github.com/topjohnwu/Magisk): Топовый инструмент для root.  
- [genuine](https://github.com/brevent/genuine/): Валидация подписи APK v2.  
- [Diamorphine](https://github.com/m0nad/Diamorphine): Некоторые навыки rootkit.  
- [KernelSU](https://github.com/tiann/KernelSU): Спасибо tiann, без него KernelSU Next не релизнулся бы.  
- [Magic Mount Port](https://github.com/5ec1cff/KernelSU/blob/main/userspace/ksud/src/magic_mount.rs): 💜 5ec1cff за сохранение KernelSU!
