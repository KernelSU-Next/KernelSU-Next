[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [Український](README_UA.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | **Italiano** | [Polski](README_PL.md)  | [Български](README_BG.md) | [日本語](README_JA.md)

# KernelSU Next

<img src="/assets/kernelsu_next.png" style="width: 96px;" alt="logo">

Una soluzione root basata sul kernel per dispositivi Android.

[![Latest Release](https://img.shields.io/github/v/release/KernelSU-Next/KernelSU-Next?label=Release&logo=github)](https://github.com/KernelSU-Next/KernelSU-Next/releases/latest)
[![Nightly Release](https://img.shields.io/badge/Nightly%20Release-gray?logo=hackthebox&logoColor=fff)](https://nightly.link/KernelSU-Next/KernelSU-Next/workflows/build-manager-ci/next/Manager)
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-orange.svg?logo=gnu)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![GitHub License](https://img.shields.io/github/license/KernelSU-Next/KernelSU-Next?logo=gnu)](/LICENSE)

## Caratteristiche

1. Gestione degli accessi `su` e root basata sul kernel.
2. Sistema modulare basato sul sistema di montaggio dinamico [Magic Mount](https://topjohnwu.github.io/Magisk/details.html#magic-mount) / [OverlayFS](https://en.wikipedia.org/wiki/OverlayFS).
3. [App Profile](https://kernelsu.org/guide/app-profile.html): Rinchiudi il potere della radice in una gabbia.

## Stato compatibilità

KernelSU Next supporta ufficialmente la maggior parte dei kernel Android dalla versione 4.4 alla 6.6.
 - I kernel GKI 2.0 (5.10+) possono eseguire immagini precostruite e LKM/KMI.
 - I kernel GKI 1.0 (4.19 - 5.4) devono essere ricostruiti con il driver KernelSU.
 - Anche i kernel EOL (<4.14) devono essere ricostruiti con il driver KernelSU (la versione 3.18+ è sperimentale e potrebbe richiedere alcuni backport di funzioni).

Attualmente è supportata solo l'architettura `arm64-v8a`, `armeabi-v7a` & `x86_64`.

## Utilizzo

- [Istruzioni per l'installazione](https://ksunext.org/pages/installation.html)

## Security

Per informazioni sulla segnalazione delle vulnerabilità di sicurezza in KernelSU, vedere [SECURITY.md](/SECURITY.md).

## Licenza

- I file nella directory `kernel` sono [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).
- Tutte le altre parti eccetto la directory `kernel` sono [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html).

## Donazioni

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ USDT BEP20 ]

- TYUVMWGTcnR5svnDoX85DWHyqUAeyQcdjh [ USDT TRC20 ]

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ USDT ERC20 ]

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ ETH ERC20 ]

- Ld238uYBuRQdZB5YwdbkuU6ektBAAUByoL [ LTC ]

- 19QgifcjMjSr1wB2DJcea5cxitvWVcXMT6 [ BTC ]

## Crediti

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/): L'idea di KernelSU.
- [Magisk](https://github.com/topjohnwu/Magisk): Il potente strumento di root.
- [genuine](https://github.com/brevent/genuine/): Convalida della firma APK v2.
- [Diamorphine](https://github.com/m0nad/Diamorphine): Alcune competenze sui rootkit.
- [KernelSU](https://github.com/tiann/KernelSU): Grazie a tiann, altrimenti KernelSU Next non esisterebbe nemmeno.
- [Magic Mount Port](https://github.com/5ec1cff/KernelSU/blob/main/userspace/ksud/src/magic_mount.rs): 💜 5ec1cff per aver salvato KernelSU!
