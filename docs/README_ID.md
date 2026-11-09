[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | **Bahasa Indonesia** | [Русский](README_RU.md) | [Українська](README_UA.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | [日本語](README_JA.md) | [Español](README_ES.md)

---

<div align="center">
  <img src="/assets/kernelsu_next.png" width="96" alt="KernelSU Next Logo">

  <h2>KernelSU Next</h2>
  <p><strong>Solusi root berbasis kernel untuk perangkat Android.</strong></p>

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

## 🚀 Fitur

- Manajemen akses root dan `su` berbasis kernel.
- Sistem modul berbasis [metamodules](https://kernelsu.org/guide/metamodule.html): infrastruktur yang dapat dipasang untuk modifikasi tanpa sistem.
- Pemuatan driver SUSFS LKM bawaan untuk perangkat GKI 2.0.
- Deteksi risiko yang memblokir instalasi modul mencurigakan.
- Dukungan pencadangan dan pemulihan modul serta profil aplikasi.
- [Profil Aplikasi](https://kernelsu.org/guide/app-profile.html): membatasi kekuatan root dalam sangkar.

---

## ✅ Kompatibilitas

KernelSU Next mendukung kernel Android dari **4.4 hingga 6.18**.

| Versi kernel | Catatan dukungan |
|----------------------|-------------------------------------------------------------------------|
| 5.10+ (GKI 2.0)      | Mendukung gambar yang telah dibuat sebelumnya dan LKM/KMI |
| 4.19 – 5.4 (GKI 1.0) | Membutuhkan driver KernelSU yang tertanam di kernel |
| < 4.14 (EOL)         | Membutuhkan driver KernelSU (3.18+ masih eksperimental dan mungkin perlu backport) |

**Arsitektur yang didukung:** `arm64-v8a` dan `x86_64`

> [!CAUTION]
> Versi kernel terbaru telah menerapkan perubahan yang merusak yang menyebabkan KernelSU Next gagal dan berpotensi memicu kernel panic pada `x86_64`! Periksa situs web untuk informasi lebih lanjut!

---

## 📦 Instalasi

Silakan merujuk ke panduan [Instalasi](https://kernelsu-next.github.io/webpage/pages/installation.html) untuk instruksi penyiapan.

---

## 🏅 Kontribusi

- Buka [Crowdin](https://crowdin.com/project/kernelsu-next) untuk mengirim terjemahan untuk manajer!
- Untuk melaporkan masalah keamanan, lihat [SECURITY.md](/SECURITY.md).

---

## 📜 Lisensi

- **direktori `/kernel`:** [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).
- **Semua file lain:** [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html).

---

## 💸 Donasi

Jika Anda ingin mendukung proyek ini:

- **USDT (BEP20, ERC20)**: `0x0b269716dc00692676e4217e23c7f67abd5f0f3e`
- **USDT (TRC20)**: `TXu6VX8dvJLQPx4WBobjbYT5tefHTDikWo`
- **LTC**: `LMYRqRXPKAFpYncBhNS44W1iwFit14LH1u`

---

## 🙏 Kredit

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/) – Inspirasi konsep
- [Magisk](https://github.com/topjohnwu/Magisk) – Implementasi root inti
- [Genuine](https://github.com/brevent/genuine/) – Validasi tanda tangan APK v2
- [Diamorphine](https://github.com/m0nad/Diamorphine) – Teknik rootkit
- [KernelSU](https://github.com/tiann/KernelSU) – Basis asli yang membuat KernelSU Next mungkin terjadi
- [Susfs4ksu](https://gitlab.com/simonpunk/susfs4ksu) – Ekstensi penyembunyian root level kernel untuk KernelSU
- [Susfs4ksu-lkm](https://github.com/inforcqb/susfs4ksu-lkm) – SUSFS yang diporting sebagai modul kernel yang dapat dimuat

