[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [Українська](README_UA.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | [日本語](README_JA.md) | **Español**

---

<div align="center">
  <img src="/assets/kernelsu_next.png" width="96" alt="KernelSU Next Logo">

  <h2>KernelSU Next</h2>
  <p><strong>Una solución root basada en el kernel para dispositivos Android.</strong></p>

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

## 🚀 Funciones

- Gestión de acceso root y `su` basado en el kernel.
- Sistema de módulos basado en [metamodules](https://kernelsu.org/guide/metamodule.html): infraestructura conectable para modificaciones sin sistema.
- Carga integrada del controlador SUSFS LKM para dispositivos GKI 2.0.
- Detección de riesgos que bloquea la instalación de módulos sospechosos.
- Compatibilidad con copias de seguridad y restauración de módulos y perfiles de aplicaciones.
- [Perfil de la aplicación](https://kernelsu.org/guide/app-profile.html): encierra el poder root en una jaula.

---

## ✅ Compatibilidad

KernelSU Next admite kernels de Android desde **4.4 hasta 6.18**.

| Versión del kernel | Notas de compatibilidad |
|----------------------|-------------------------------------------------------------------------|
| 5.10+ (GKI 2.0)      | Admite imágenes precompiladas y LKM/KMI |
| 4.19 – 5.4 (GKI 1.0) | Requiere que el controlador de KernelSU esté integrado en el kernel |
| < 4.14 (EOL)         | Requiere el controlador de KernelSU (3.18+ es experimental y puede requerir backports) |

**Arquitecturas compatibles:** `arm64-v8a` y `x86_64`

> [!CAUTION]
> Las versiones recientes de kernel han implementado un cambio que rompe la compatibilidad, causando que KernelSU Next falle y potencialmente provoque un kernel panic en `x86_64`! Consulta el sitio web para obtener más información.

---

## 📦 Instalación

Consulta la guía de [Instalación](https://kernelsu-next.github.io/webpage/pages/installation.html) para obtener instrucciones de configuración.

---

## 🏅 Contribución

- Visita [Crowdin](https://crowdin.com/project/kernelsu-next) para enviar una traducción del gestor.
- Para informar sobre problemas de seguridad, consulta [SECURITY.md](/SECURITY.md).

---

## 📜 Licencia

- **directorio `/kernel`:** [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).
- **Todos los demás archivos:** [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html).

---

## 💸 Donaciones

Si quieres apoyar el proyecto:

- **USDT (BEP20, ERC20)**: `0x0b269716dc00692676e4217e23c7f67abd5f0f3e`
- **USDT (TRC20)**: `TXu6VX8dvJLQPx4WBobjbYT5tefHTDikWo`
- **LTC**: `LMYRqRXPKAFpYncBhNS44W1iwFit14LH1u`

---

## 🙏 Créditos

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/) – Inspiración conceptual
- [Magisk](https://github.com/topjohnwu/Magisk) – Implementación principal de root
- [Genuine](https://github.com/brevent/genuine/) – Validación de la firma APK v2
- [Diamorphine](https://github.com/m0nad/Diamorphine) – Técnicas de rootkit
- [KernelSU](https://github.com/tiann/KernelSU) – La base original que hizo posible KernelSU Next
- [Susfs4ksu](https://gitlab.com/simonpunk/susfs4ksu) – Extensión de ocultación de root a nivel de kernel para KernelSU
- [Susfs4ksu-lkm](https://github.com/inforcqb/susfs4ksu-lkm) – SUSFS portado como módulo de kernel cargable

