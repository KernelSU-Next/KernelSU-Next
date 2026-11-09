[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [Українська](README_UA.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | **Italiano** | [Polski](README_PL.md) | [Български](README_BG.md) | [日本語](README_JA.md) | [Español](README_ES.md)

---

<div align="center">
  <img src="/assets/kernelsu_next.png" width="96" alt="KernelSU Next Logo">

  <h2>KernelSU Next</h2>
  <p><strong>Una soluzione root basata sul kernel per dispositivi Android.</strong></p>

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

## 🚀 Funzionalità

- Gestione dell'accesso root e `su` basata sul kernel.
- Sistema di moduli basato su [metamodules](https://kernelsu.org/guide/metamodule.html): infrastruttura plug-in per modifiche senza sistema.
- Caricamento integrato del driver SUSFS LKM per dispositivi GKI 2.0.
- Rilevamento del rischio che blocca l’installazione di moduli sospetti.
- Supporto per il backup e il ripristino di moduli e profili app.
- [Profilo app](https://kernelsu.org/guide/app-profile.html): racchiude il potere del root in una gabbia.

---

## ✅ Compatibilità

KernelSU Next supporta kernel Android da **4.4 a 6.18**.

| Versione del kernel | Note di supporto |
|----------------------|-------------------------------------------------------------------------|
| 5.10+ (GKI 2.0)      | Supporta immagini precompilate e LKM/KMI |
| 4.19 – 5.4 (GKI 1.0) | Richiede il driver KernelSU integrato nel kernel |
| < 4.14 (EOL)         | Richiede il driver KernelSU (3.18+ è sperimentale e potrebbe richiedere backport) |

**Architetture supportate:** `arm64-v8a` e `x86_64`

> [!CAUTION]
> Le versioni recenti del kernel hanno introdotto una modifica incompatibile che fa fallire KernelSU Next e può causare un kernel panic su `x86_64`! Controlla il sito web per ulteriori informazioni!

---

## 📦 Installazione

Per le istruzioni di configurazione, consulta la guida [Installazione](https://kernelsu-next.github.io/webpage/pages/installation.html).

---

## 🏅 Contributi

- Vai su [Crowdin](https://crowdin.com/project/kernelsu-next) per inviare una traduzione per il manager!
- Per segnalare problemi di sicurezza, consulta [SECURITY.md](/SECURITY.md).

---

## 📜 Licenza

- **directory `/kernel`:** [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).
- **Tutti gli altri file:** [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html).

---

## 💸 Donazioni

Se vuoi supportare il progetto:

- **USDT (BEP20, ERC20)**: `0x0b269716dc00692676e4217e23c7f67abd5f0f3e`
- **USDT (TRC20)**: `TXu6VX8dvJLQPx4WBobjbYT5tefHTDikWo`
- **LTC**: `LMYRqRXPKAFpYncBhNS44W1iwFit14LH1u`

---

## 🙏 Crediti

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/) – Ispirazione concettuale
- [Magisk](https://github.com/topjohnwu/Magisk) – Implementazione root core
- [Genuine](https://github.com/brevent/genuine/) – Validazione firma APK v2
- [Diamorphine](https://github.com/m0nad/Diamorphine) – Tecniche rootkit
- [KernelSU](https://github.com/tiann/KernelSU) – La base originale che ha reso possibile KernelSU Next
- [Susfs4ksu](https://gitlab.com/simonpunk/susfs4ksu) – Estensione per nascondere root a livello kernel per KernelSU
- [Susfs4ksu-lkm](https://github.com/inforcqb/susfs4ksu-lkm) – SUSFS portato come modulo kernel caricabile

