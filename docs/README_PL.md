[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [Українська](README_UA.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | **Polski** | [Български](README_BG.md) | [日本語](README_JA.md)

---

<div align="center">
  <img src="/assets/kernelsu_next.png" width="96" alt="KernelSU Next Logo">

  <h2>KernelSU Next</h2>
  <p><strong>Bazujące na jądrze rozwiązanie root dla urządzeń z Androidem.</strong></p>

  <p>
    <a href="https://github.com/KernelSU-Next/KernelSU-Next/releases/latest">
      <img src="https://img.shields.io/github/v/release/KernelSU-Next/KernelSU-Next?label=Release&logo=github" alt="Latest Release">
    </a>
    <a href="https://nightly.link/KernelSU-Next/KernelSU-Next/workflows/build-manager-ci/next/Manager">
      <img src="https://img.shields.io/badge/Nightly%20Release-gray?logo=hackthebox&logoColor=fff" alt="Nightly Build">
    </a>
    <a href="https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html">
      <img src="https://img.shields.io/badge/License-GPL%20v2-orange.svg?logo=gnu" alt="License: GPL v2">
    </a>
    <a href="/LICENSE">
      <img src="https://img.shields.io/github/license/KernelSU-Next/KernelSU-Next?logo=gnu" alt="GitHub License">
    </a>
  </p>
</div>

---

## 🚀 Funkcjonalności

- Oparte na jądrze `su` i zarządzanie dostępem do roota.
- System modułów oparty na [Magic Mount](https://topjohnwu.github.io/Magisk/details.html#magic-mount) i [OverlayFS](https://en.wikipedia.org/wiki/OverlayFS).
- [Profil aplikacji](https://kernelsu.org/guide/app-profile.html): Ograniczaj uprawnienia roota dla poszczególnych aplikacji.

---

## ✅ Kompatybilność

KernelSU Next obsługuje jądra Androida od wersji **4.4 do 6.6**:

| Wersja jądra         | Informacje techniczne                                                                     |
|----------------------|-------------------------------------------------------------------------------------------|
| 5.10+ (GKI 2.0)      | Obsługuje wstępnie skompilowane obrazy i LKM/KMI                                          |
| 4.19 – 5.4 (GKI 1.0) | Wymaga wbudowania sterownika KernelSU                                                     | 
| < 4.14 (EOL)         | Wymaga sterownika KernelSU (obsługa 3.18+ jest eksperymentalna i może wymagać backportów) |

**Obsługiwane architektury:** `arm64-v8a`, `armeabi-v7a` i `x86_64`

---

## 📦 Instalacja

Instrukcje dotyczące instalacji można znaleźć w przewodniku [Instalacja](https://kernelsu-next.github.io/webpage/pages/installation.html).

---

## 🔐 Bezpieczeństwo

Aby zgłosić problemy związane z bezpieczeństwem, zapoznaj się z [SECURITY.md](/SECURITY.md).

---

## 📜 Licencje

- **katalog `/kernel`:** [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).
- **Wszystkie pozostałe pliki:** [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html).

---

## 💸 Darowizny

Jeśli chciałbyś wesprzeć projekt:

- **USDT (BEP20, ERC20)**: `0x12b5224b7aca0121c2f003240a901e1d064371c1`
- **USDT (TRC20)**: `TYUVMWGTcnR5svnDoX85DWHyqUAeyQcdjh`
- **ETH (ERC20)**: `0x12b5224b7aca0121c2f003240a901e1d064371c1`
- **LTC**: `Ld238uYBuRQdZB5YwdbkuU6ektBAAUByoL`
- **BTC**: `19QgifcjMjSr1wB2DJcea5cxitvWVcXMT6`

---

## 🙏 Podziękowania

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/) – Inspiracja konceptem
- [Magisk](https://github.com/topjohnwu/Magisk) – Bazowa implementacja roota
- [Genuine](https://github.com/brevent/genuine/) – Walidacja podpisu APK v2
- [Diamorphine](https://github.com/m0nad/Diamorphine) – Techniki rootkit
- [KernelSU](https://github.com/tiann/KernelSU) – Oryginalna baza, która umożliwiła powstanie KernelSU Next
- [Magic Mount Port](https://github.com/5ec1cff/KernelSU/blob/main/userspace/ksud/src/magic_mount.rs) – 💜 dla 5ec1cff za utrzymanie KernelSU przy życiu
