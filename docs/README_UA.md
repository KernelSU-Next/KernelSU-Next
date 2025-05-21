[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | **Український** | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | [日本語](README_JA.md)

# KernelSU Next

<img src="/assets/kernelsu_next.png" style="width: 96px;" alt="logo">

Root-рішення для Android на базі ядра. 

[![Latest Release](https://img.shields.io/github/v/release/KernelSU-Next/KernelSU-Next?label=Release&logo=github)](https://github.com/KernelSU-Next/KernelSU-Next/releases/latest)
[![Nightly Release](https://img.shields.io/badge/Nightly%20Release-gray?logo=hackthebox&logoColor=fff)](https://nightly.link/KernelSU-Next/KernelSU-Next/workflows/build-manager-ci/next/Manager)
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-orange.svg?logo=gnu)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![GitHub License](https://img.shields.io/github/license/KernelSU-Next/KernelSU-Next?logo=gnu)](/LICENSE)

## Функції

1. Реалізація `su` та керування root-доступом безпосередньо на рівні ядра.  
2. Динамічна система модулів, побудована на [Magic Mount](https://topjohnwu.github.io/Magisk/details.html#magic-mount) / [OverlayFS](https://en.wikipedia.org/wiki/OverlayFS).  
3. [Профіль для застосунків](https://kernelsu.org/guide/app-profile.html): дозволяє обмежити root-доступ у пісочниці для окремих застосунків.

## Сумісність

KernelSU Next працює з більшістю ядер Android (4.4 – 6.6):  
- GKI 2.0 (5.10+) можуть використовувати попередньо зібрані образи та LKM/KMI.  
- GKI 1.0 (4.19 – 5.4) вимагають перескладання з драйвером KernelSU.  
- EOL (<4.14) також вимагають перескладання з драйвером KernelSU (версії 3.18+ є експериментальними і можуть потребувати деякі функції з бэкпортів).  

Наразі підтримується лише `arm64-v8a`.

## Використання

- [Інструкція з встановлення](https://ksunext.org/internals/installation.html)

## Безпека

Якщо ви знайшли баг, перегляньте [SECURITY.md](/SECURITY.md) — там є інструкція, як повідомити про проблему.

## Ліцензія

- Усе, що в директорії `kernel`, — [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).  
- Інший код, за винятком директорії `kernel`, під [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html).

## Донати

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ USDT BEP20 ]

- TYUVMWGTcnR5svnDoX85DWHyqUAeyQcdjh [ USDT TRC20 ]

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ USDT ERC20 ]

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ ETH ERC20 ]

- Ld238uYBuRQdZB5YwdbkuU6ektBAAUByoL [ LTC ]

- 19QgifcjMjSr1wB2DJcea5cxitvWVcXMT6 [ BTC ]

## Подяка

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/): Ідея KernelSU.  
- [Magisk](https://github.com/topjohnwu/Magisk): Топовий інструмент для root-доступу.  
- [genuine](https://github.com/brevent/genuine/): Валідація підпису APK v2.  
- [Diamorphine](https://github.com/m0nad/Diamorphine): Деякі навички rootkit.  
- [KernelSU](https://github.com/tiann/KernelSU): Дякуємо tiann, без нього KernelSU Next навіть не існував би.  
- [Magic Mount Port](https://github.com/5ec1cff/KernelSU/blob/main/userspace/ksud/src/magic_mount.rs): 💜 5ec1cff за збереження KernelSU!
