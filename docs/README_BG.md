[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | **Български** | [Magyar](README_HU.md)

# KernelSU Next

<img src="/assets/kernelsu_next.png" style="width: 96px;" alt="лого">

Ядрено решение за root достъп за Android устройства.

[![Последна версия](https://img.shields.io/github/v/release/KernelSU-Next/KernelSU-Next?label=Версия&logo=github)](https://github.com/KernelSU-Next/KernelSU-Next/releases/latest)
[![Нощна версия](https://img.shields.io/badge/Нощна_версия-сива?logo=hackthebox&logoColor=fff)](https://nightly.link/KernelSU-Next/KernelSU-Next/workflows/build-manager-ci/next/Manager)
[![Лиценз: GPL v2](https://img.shields.io/badge/Лиценз-GPL%20v2-оранжев.svg?logo=gnu)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![Лиценз в GitHub](https://img.shields.io/github/license/KernelSU-Next/KernelSU-Next?logo=gnu)](/LICENSE)

## Възможности

1. Управление на `su` и root достъп на ядрено ниво
2. Система за модули базирана на [Magic Mount](https://topjohnwu.github.io/Magisk/details.html#magic-mount) / [OverlayFS](https://en.wikipedia.org/wiki/OverlayFS)
3. [Профили за приложения](https://kernelsu.org/guide/app-profile.html): Ограничаване на root права за конкретни приложения

## Съвместимост

KernelSU Next официално поддържа повечето Android ядра от версия 4.4 до 6.6:
- Ядра GKI 2.0 (5.10+) могат да използват предварително компилирани изображения и LKM/KMI
- Ядра GKI 1.0 (4.19 - 5.4) изискват прекомпилиране с драйвера на KernelSU
- Остарели ядра (<4.14) също изискват прекомпилиране (3.18+ е експериментална поддръжка)

В момента се поддържа само архитектурата `arm64-v8a`.

## Инсталация

- [Инструкции за инсталиране](https://ksunext.org/pages/installation.html)

## Сигурност

За докладване на уязвимости вижте [SECURITY.md](/SECURITY.md).

## Лиценз

- Файловете в директорията `kernel` са [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
- Всички останали файлове са [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html)

## Дарения

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ USDT BEP20 ]
- TYUVMWGTcnR5svnDoX85DWHyqUAeyQcdjh [ USDT TRC20 ]
- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ USDT ERC20 ]
- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ ETH ERC20 ]
- Ld238uYBuRQdZB5YwdbkuU6ektBAAUByoL [ LTC ]
- 19QgifcjMjSr1wB2DJcea5cxitvWVcXMT6 [ BTC ]

## Благодарности

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/): Идеята за KernelSU
- [Magisk](https://github.com/topjohnwu/Magisk): Мощният root инструмент
- [genuine](https://github.com/brevent/genuine/): Валидация на APK подписи v2
- [Diamorphine](https://github.com/m0nad/Diamorphine): Rootkit техники
- [KernelSU](https://github.com/tiann/KernelSU): Благодарности към tiann за създаването на KernelSU
- [Magic Mount Port](https://github.com/5ec1cff/KernelSU/blob/main/userspace/ksud/src/magic_mount.rs): 💜 5ec1cff за спасяването на KernelSU
