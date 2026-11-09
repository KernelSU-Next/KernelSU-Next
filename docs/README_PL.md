[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [Українська](README_UA.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | **Polski** | [Български](README_BG.md) | [日本語](README_JA.md) | [Español](README_ES.md)

---

<div align="center">
  <img src="/assets/kernelsu_next.png" width="96" alt="KernelSU Next Logo">

  <h2>KernelSU Next</h2>
  <p><strong>Rozwiązanie root oparte na jądrze dla urządzeń z Androidem.</strong></p>

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

## 🚀 Funkcje

- Zarządzanie dostępem root i `su` opartym na jądrze.
- System modułów oparty na [metamodules](https://kernelsu.org/guide/metamodule.html): rozszerzalna infrastruktura dla modyfikacji bez systemu.
- Wbudowane ładowanie sterownika SUSFS LKM dla urządzeń GKI 2.0.
- Wykrywanie ryzyka blokujące instalację podejrzanych modułów.
- Obsługa tworzenia kopii zapasowych i przywracania modułów oraz profili aplikacji.
- [Profil aplikacji](https://kernelsu.org/guide/app-profile.html): zamyka moc root w klatce.

---

## ✅ Kompatybilność

KernelSU Next obsługuje jądra Android od **4.4 do 6.18**.

| Wersja jądra | Uwagi dotyczące wsparcia |
|----------------------|-------------------------------------------------------------------------|
| 5.10+ (GKI 2.0)      | Obsługuje wstępnie zbudowane obrazy i LKM/KMI |
| 4.19 – 5.4 (GKI 1.0) | Wymaga sterownika KernelSU zintegrowanego z jądrem |
| < 4.14 (EOL)         | Wymaga sterownika KernelSU (3.18+ jest eksperymentalne i może wymagać backportów) |

**Obsługiwane architektury:** `arm64-v8a` i `x86_64`

> [!CAUTION]
> Nowe wersje jądra wprowadziły zmiany powodujące awarie KernelSU Next oraz możliwe wywołanie kernel panic na `x86_64`! Sprawdź stronę internetową, aby uzyskać więcej informacji!

---

## 📦 Instalacja

Zobacz przewodnik [Instalacja](https://kernelsu-next.github.io/webpage/pages/installation.html), aby uzyskać instrukcje konfiguracji.

---

## 🏅 Wkład

- Wejdź na [Crowdin](https://crowdin.com/project/kernelsu-next), aby przesłać tłumaczenie dla menedżera!
- Aby zgłosić problemy z bezpieczeństwem, zapoznaj się z [SECURITY.md](/SECURITY.md).

---

## 📜 Licencja

- **katalog `/kernel`:** [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).
- **Wszystkie pozostałe pliki:** [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html).

---

## 💸 Darowizny

Jeśli chcesz wesprzeć projekt:

- **USDT (BEP20, ERC20)**: `0x0b269716dc00692676e4217e23c7f67abd5f0f3e`
- **USDT (TRC20)**: `TXu6VX8dvJLQPx4WBobjbYT5tefHTDikWo`
- **LTC**: `LMYRqRXPKAFpYncBhNS44W1iwFit14LH1u`

---

## 🙏 Podziękowania

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/) – Inspiracja koncepcyjna
- [Magisk](https://github.com/topjohnwu/Magisk) – Główna implementacja root
- [Genuine](https://github.com/brevent/genuine/) – Weryfikacja podpisu APK v2
- [Diamorphine](https://github.com/m0nad/Diamorphine) – Techniki rootkit
- [KernelSU](https://github.com/tiann/KernelSU) – Oryginalna baza, która umożliwiła powstanie KernelSU Next
- [Susfs4ksu](https://gitlab.com/simonpunk/susfs4ksu) – Rozszerzenie ukrywania root na poziomie jądra dla KernelSU
- [Susfs4ksu-lkm](https://github.com/inforcqb/susfs4ksu-lkm) – SUSFS przeniesiony jako ładowalny moduł jądra

