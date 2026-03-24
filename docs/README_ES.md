[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [Українська](README_UA.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | [日本語](README_JA.md) | **Español**

---

<div align="center">
  <img src="/assets/kernelsu_next.png" width="96" alt="KernelSU Next Logo">

  <h2>KernelSU Next</h2>
  <p><strong>Una solución de root basada en el kernel para tus dispositivos Android.</strong></p>

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
    <a title="Crowdin" target="_blank" href="https://crowdin.com/project/kernelsu-next"><img src="https://badges.crowdin.net/kernelsu-next/localized.svg"></a>
  </p>
</div>

---

## 🚀 Características

- `su` y gestión de acceso root basados en el kernel.
- Sistema de módulos basado en [Magic Mount](https://topjohnwu.github.io/Magisk/details.html#magic-mount) y [OverlayFS](https://en.wikipedia.org/wiki/OverlayFS).
- [Perfil de Aplicación](https://kernelsu.org/guide/app-profile.html): Limita los privilegios de root por aplicación.

---

## ✅ Compatibilidad

KernelSU Next es compatible con kernels de Android desde la versión **4.4 hasta la 6.6**.

| Versión del kernel   | Notas de soporte                                                                  |
|----------------------|-----------------------------------------------------------------------------------|
| 5.10+ (GKI 2.0)      | Admite imágenes precompiladas y LKM/KMI                                           |
| 4.19 – 5.4 (GKI 1.0) | Requiere que el driver de KernelSU esté integrado                                 |
| < 4.14 (EOL)         | Requiere el driver de KernelSU (3.18+ es experimental y puede necesitar backports |

**Arquitecturas compatibles:** `arm64-v8a`, `armeabi-v7a` y `x86_64`

> [!CAUTION]
> ¡Las versiones recientes del kernel han implementado un cambio que rompe la compatibilidad, causando que KernelSU Next falle y potencialmente provoque un kernel panic en `x86_64`! ¡Consulta el sitio web para más información!

---

## 📦 Instalación

Por favor, consulta la guía de [Instalación](https://kernelsu-next.github.io/webpage/pages/installation.html) para ver las instrucciones de configuración.

---

## 🏅 Contribución

Para informar sobre problemas de seguridad, por favor, consulta [SECURITY.md](/SECURITY.md).

---

## 📜 Licencia

- **Directorio `/kernel`:** [Solo-GPL-2.0](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).
- **Todos los demás archivos:** [GPL-3.0-o-superior](https://www.gnu.org/licenses/gpl-3.0.html).

---

## 💸 Donaciones

Si te gustaría apoyar el proyecto:

- **USDT (BEP20, ERC20)**: `0x12b5224b7aca0121c2f003240a901e1d064371c1`
- **USDT (TRC20)**: `TYUVMWGTcnR5svnDoX85DWHyqUAeyQcdjh`
- **USDT (SOL)**: `A4wqBXYd6Ey4nK4SJ2bmjeMgGyaLKT9TwDLh8BEo8Zu6`
- **ETH (ERC20)**: `0x12b5224b7aca0121c2f003240a901e1d064371c1`
- **LTC**: `Ld238uYBuRQdZB5YwdbkuU6ektBAAUByoL`
- **BTC**: `19QgifcjMjSr1wB2DJcea5cxitvWVcXMT6`

---

## 🙏 Créditos

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/) – Inspiración para el concepto
- [Magisk](https://github.com/topjohnwu/Magisk) – Implementación principal del root
- [Genuine](https://github.com/brevent/genuine/) – Validación de la firma v2 de los APK
- [Diamorphine](https://github.com/m0nad/Diamorphine) – Técnicas de rootkit
- [KernelSU](https://github.com/tiann/KernelSU) – La base original que hizo posible KernelSU Next
- [Magic Mount Port](https://github.com/5ec1cff/KernelSU/blob/main/userspace/ksud/src/magic_mount.rs) – 💜 a 5ec1cff por mantener vivo KernelSU
