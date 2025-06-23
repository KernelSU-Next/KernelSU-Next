[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | [日本語](README_JA.md) | **Українська**

# KernelSU Next

<img src="/assets/kernelsu_next.png" style="width: 96px;" alt="logo">

Рут-рішення на основі ядра для пристроїв Android.

[![Останній реліз](https://img.shields.io/github/v/release/KernelSU-Next/KernelSU-Next?label=Release&logo=github)](https://github.com/KernelSU-Next/KernelSU-Next/releases/latest)
[![Нічний реліз (Нестабільний)](https://img.shields.io/badge/Nightly%20Release-gray?logo=hackthebox&logoColor=fff)](https://nightly.link/KernelSU-Next/KernelSU-Next/workflows/build-manager-ci/next/Manager)
[![Ліцензія: GPL v2](https://img.shields.io/badge/License-GPL%20v2-orange.svg?logo=gnu)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![GitHub Ліцензія](https://img.shields.io/github/license/KernelSU-Next/KernelSU-Next?logo=gnu)](/LICENSE)

## Можливості

1. `su` на основі ядра та можливість контролювати дозволи руту.
2. Module system based on dynamic mount system [Magic Mount](https://topjohnwu.github.io/Magisk/details.html#magic-mount) / [OverlayFS](https://en.wikipedia.org/wiki/OverlayFS).
3. [Профілі додатків](https://kernelsu.org/guide/app-profile.html): Обмеж права руту для додатків.

## Compatibility state

KernelSU Next офіційно підтримує більшість Android ядер починаючи з 4.4 і до 6.6.
 - Користувачі GKI 2.0 (5.10+) ядра можуть використовувати готові образи та LKM/KMI.
 - Користувачі GKI 1.0 (4.19 - 5.4) ядра мають бути перезібрані з драйвером KernelSU.
 - Користувачі EOL (<4.14) ядра також мають бути перезібрані з драйвером KernelSU (Підтримка 3.18+ експерементальна і потребує бекпортів деяких функцій в ядрі).

На даний момент підтримується лише архітектура `arm64-v8a`, `armeabi-v7a` & `x86_64`.

## Спосіб використання

- [Інструкція для встановлення/інтеграції](https://ksunext.org/pages/installation.html)

## Безпека

Для інформації зв'язаною з безпекою дивіться [SECURITY.md](/SECURITY.md).

## Ліцензія

- Всі файли в директорії `kernel` мають ліцензію [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).
- Всі інші файли виключаючи директорію `kernel` мають ліцензію [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html).

## Підтримка розробника

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ USDT BEP20 ]

- TYUVMWGTcnR5svnDoX85DWHyqUAeyQcdjh [ USDT TRC20 ]

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ USDT ERC20 ]

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ ETH ERC20 ]

- Ld238uYBuRQdZB5YwdbkuU6ektBAAUByoL [ LTC ]

- 19QgifcjMjSr1wB2DJcea5cxitvWVcXMT6 [ BTC ]

## Подяки

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/): Ідея KernelSU.
- [Magisk](https://github.com/topjohnwu/Magisk): Потужний засіб руту.
- [genuine](https://github.com/brevent/genuine/): Перевірка підпису APK v2.
- [Diamorphine](https://github.com/m0nad/Diamorphine): Деякі руткіт скіли.
- [KernelSU](https://github.com/tiann/KernelSU): Дякую tiann, інакше KernelSU Next ніколи б не існував.
- [Magic Mount Port](https://github.com/5ec1cff/KernelSU/blob/main/userspace/ksud/src/magic_mount.rs): Дякую 💜 5ec1cff за збереження KernelSU!