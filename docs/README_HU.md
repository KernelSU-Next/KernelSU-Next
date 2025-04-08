[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | **Magyar**

# KernelSU Next

<img src="/assets/kernelsu_next.png" style="width: 96px;" alt="logo">

Kernel alapú root megoldás Android eszközökhöz.

[![Legutóbbi kiadás](https://img.shields.io/github/v/release/KernelSU-Next/KernelSU-Next?label=Kiadás&logo=github)](https://github.com/KernelSU-Next/KernelSU-Next/releases/latest)
[![Nightly kiadás](https://img.shields.io/badge/Nightly%20Release-gray?logo=hackthebox&logoColor=fff)](https://nightly.link/KernelSU-Next/KernelSU-Next/workflows/build-manager-ci/next/Manager)
[![Licensz: GPL v2](https://img.shields.io/badge/License-GPL%20v2-orange.svg?logo=gnu)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![GitHub Licensz](https://img.shields.io/github/license/KernelSU-Next/KernelSU-Next?logo=gnu)](/LICENSE)

## Funkciók

1. Kernel alapú `su` és root hozzáférés-kezelés.
2. Dinamikus csatolási rendszeren alapuló modulrendszer: [Magic Mount](https://topjohnwu.github.io/Magisk/details.html#magic-mount) / [OverlayFS](https://en.wikipedia.org/wiki/OverlayFS).
3. [App Profilok](https://kernelsu.org/guide/app-profile.html): Zárd el a rootot egy ketrecbe.


## Kompatibilitási állapot

A KernelSU Next hivatalosan támogatja a legtöbb Android kernelt 4.4-től 6.6-ig.
 - A GKI 2.0 (5.10+) kernelek tudnak előre elkészített képeket és LKM/KMI-t futtatni.
 - A GKI 1.0 (4.19 - 5.4) kerneleleket a KernelSU driverrel újra kell fordítani.
 - Az elavultnak számító (<4.14) kerneleket szintén újra kell fordítani a KernelSU driverrel (a 3.18+ kísérleti, és előfordulhat, hogy bizonyos funkciókat vissza kell állítani).

Jelenleg csak az `arm64-v8a` architektúra támogatott.

## Felhasználás

- [Telepítési útmutató](https://ksunext.org/pages/installation.html)

## Biztonság

A KernelSU biztonsági réseinek jelentésével kapcsolatos információkat lásd: [SECURITY.md](/SECURITY.md).

## Licensz
- A `kernel` könyvtárban található fájlok [csak GPL-2.0-ásak](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).
- A `kernel` könyvtár kivételével minden más rész [GPL-3.0-vagy újabb](https://www.gnu.org/licenses/gpl-3.0.html).

## Projekt támogatás

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ USDT BEP20 ]

- TYUVMWGTcnR5svnDoX85DWHyqUAeyQcdjh [ USDT TRC20 ]

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ USDT ERC20 ]

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ ETH ERC20 ]

- Ld238uYBuRQdZB5YwdbkuU6ektBAAUByoL [ LTC ]

- 19QgifcjMjSr1wB2DJcea5cxitvWVcXMT6 [ BTC ]

## Köszönet

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/): A KernelSU ötlete.
- [Magisk](https://github.com/topjohnwu/Magisk): Erőteljes root eszköz.
- [genuine](https://github.com/brevent/genuine/): APK v2 aláírás ellenőrzés.
- [Diamorphine](https://github.com/m0nad/Diamorphine): Néhány rootkit készség.
- [KernelSU](https://github.com/tiann/KernelSU): Köszönet tiann-nak, különben a KernelSU Next nem is létezne.
- [Magic Mount Port](https://github.com/5ec1cff/KernelSU/blob/main/userspace/ksud/src/magic_mount.rs): Külön 💜 5ec1cff-nak hogy megmentette a KernelSU-t!
