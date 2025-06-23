[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [Український](README_UA.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | **Polski** | [Български](README_BG.md) | [日本語](README_JA.md)

# KernelSU Next

<img src="/assets/kernelsu_next.png" style="width: 96px;" alt="logo">

Bazujące na jądrze rozwiązanie root dla urządzeń z Androidem.

[![Latest Release](https://img.shields.io/github/v/release/KernelSU-Next/KernelSU-Next?label=Release&logo=github)](https://github.com/KernelSU-Next/KernelSU-Next/releases/latest)
[![Nightly Release](https://img.shields.io/badge/Nightly%20Release-gray?logo=hackthebox&logoColor=fff)](https://nightly.link/KernelSU-Next/KernelSU-Next/workflows/build-manager-ci/next/Manager)
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-orange.svg?logo=gnu)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![GitHub License](https://img.shields.io/github/license/KernelSU-Next/KernelSU-Next?logo=gnu)](/LICENSE)

## Funkcjonalności

1. Oparte na jądrze `su` i zarządzanie dostępem do roota.
2. System modułów oparty na dynamicznym systemie montowania [Magic Mount](https://topjohnwu.github.io/Magisk/details.html#magic-mount) / [OverlayFS](https://en.wikipedia.org/wiki/OverlayFS).
3. [Profil aplikacji](https://kernelsu.org/guide/app-profile.html): Ujarzmij moc roota poprzez możliwość nakładania ograniczeń na uprawnienia roota dla poszczególnych aplikacji.

## Stan zgodności

KernelSU Next oficjalnie obsługuje większość jąder Androida od wersji 4.4 do 6.6.
 - Jądra GKI 2.0 (5.10+) mogą uruchamiać wstępnie przygotowane obrazy i LKM/KMI.
 - Jądra GKI 1.0 (4.19 - 5.4) muszą zostać zrekompilowane z dodatkiem sterownika KernelSU.
 - Jądra EOL (<4.14) również muszą zostać zrekompilowane z dodatkiem sterownika KernelSU (obsługa 3.18+ jest eksperymentalna i może wymagać backportu pewnych funkcji).

Obecnie obsługiwana jest tylko architektura `arm64-v8a`, `armeabi-v7a` & `x86_64`

## Użycie

- [Instrukcja instalacji](https://ksunext.org/pages/installation.html)

## Bezpieczeństwo

Informacje na temat zgłaszania luk bezpieczeństwa w KernelSU znajdziesz w [SECURITY.md](/SECURITY.md).

## Licencje

- Pliki w katalogu `kernel` są dostępne na licencji [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).
- Wszystkie inne elementy, z wyjątkiem katalogu `kernel`, są dostępne na licencji [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html).

## Darowizny

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ USDT BEP20 ]

- TYUVMWGTcnR5svnDoX85DWHyqUAeyQcdjh [ USDT TRC20 ]

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ USDT ERC20 ]

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ ETH ERC20 ]

- Ld238uYBuRQdZB5YwdbkuU6ektBAAUByoL [ LTC ]

- 19QgifcjMjSr1wB2DJcea5cxitvWVcXMT6 [ BTC ]

## Podziękowania

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/): Idea, na której opiera się KernelSU.
- [Magisk](https://github.com/topjohnwu/Magisk): Potężne narzędzie do rootowania.
- [genuine](https://github.com/brevent/genuine/): Walidacja podpisu APK v2.
- [Diamorphine](https://github.com/m0nad/Diamorphine): Część zdolności rootkitowych.
- [KernelSU](https://github.com/tiann/KernelSU): Dzięki tiann, bez ciebie KernelSU Next w ogóle by nie istniał.
- [Magic Mount Port](https://github.com/5ec1cff/KernelSU/blob/main/userspace/ksud/src/magic_mount.rs): 💜 5ec1cff za uratowanie KernelSU!
