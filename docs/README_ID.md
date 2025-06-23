[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | **Bahasa Indonesia** | [Русский](README_RU.md) | [Український](README_UA.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | [日本語](README_JA.md)

# KernelSU Next

<img src="/assets/kernelsu_next.png" style="width: 96px;" alt="logo">

Sebuah solusi root berbasis Kernel untuk perangkat Android.

[![Latest Release](https://img.shields.io/github/v/release/KernelSU-Next/KernelSU-Next?label=Release&logo=github)](https://github.com/KernelSU-Next/KernelSU-Next/releases/latest)
[![Nightly Release](https://img.shields.io/badge/Nightly%20Release-gray?logo=hackthebox&logoColor=fff)](https://nightly.link/KernelSU-Next/KernelSU-Next/workflows/build-manager-ci/next/Manager)
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-orange.svg?logo=gnu)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![GitHub License](https://img.shields.io/github/license/KernelSU-Next/KernelSU-Next?logo=gnu)](/LICENSE)

## Fitur

1. Akses root dan manajemen `su` berbasis Kernel.
2. Sistem modul berbasis sistem mount dinamis [Magic Mount](https://topjohnwu.github.io/Magisk/details.html#magic-mount) / [OverlayFS](https://en.wikipedia.org/wiki/OverlayFS).
3. [Profil Aplikasi](https://kernelsu.org/guide/app-profile.html): Mengunci kekuatan root dalam sebuah kandang.

## Status Kompatibilitas

KernelSU Next secara resmi mendukung sebagian besar kernel Android mulai dari 4.4 hingga 6.6.
 - Kernel GKI 2.0 (5.10+) dapat menjalankan gambar yang telah dibangun sebelumnya dan LKM/KMI.
 - Kernel GKI 1.0 (4.19 - 5.4) perlu dibangun ulang dengan driver KernelSU.
 - Kernel EOL (<4.14) juga perlu dibangun ulang dengan driver KernelSU (3.18+ bersifat eksperimental dan mungkin memerlukan beberapa backport fungsi).

Saat ini, hanya `arm64-v8a`, `armeabi-v7a` & `x86_64` yang didukung.

## Penggunaan

- [Petunjuk instalasi](https://ksunext.org/pages/installation.html)

## Keamanan

Untuk informasi tentang melaporkan kerentanannya di KernelSU, lihat [SECURITY.md](/SECURITY.md).

## Lisensi

- File di bawah direktori `kernel` menggunakan lisensi [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).
- Semua bagian lain kecuali direktori `kernel` menggunakan lisensi [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html).

## Kredit

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/): Ide KernelSU.
- [Magisk](https://github.com/topjohnwu/Magisk): Alat root yang kuat.
- [genuine](https://github.com/brevent/genuine/): Validasi tanda tangan APK v2.
- [Diamorphine](https://github.com/m0nad/Diamorphine): Beberapa keterampilan rootkit.
- [KernelSU](https://github.com/tiann/KernelSU): Terima kasih kepada tiann, jika tidak, KernelSU Next bahkan tidak akan ada.
- [Magic Mount Port](https://github.com/5ec1cff/KernelSU/blob/main/userspace/ksud/src/magic_mount.rs): 💜 5ec1cff karena menyelamatkan KernelSU!
