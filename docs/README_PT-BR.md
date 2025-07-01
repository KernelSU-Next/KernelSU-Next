[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | **Português (Brasil)** | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [Український](README_UA.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | [日本語](README_JA.md)

---

<div align="center">
  <img src="/assets/kernelsu_next.png" width="96" alt="KernelSU Next Logo">

  <h2>KernelSU Next</h2>
  <p><strong>Uma solução root baseada em kernel para dispositivos Android.</strong></p>

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

## 🚀 Características

- `su` e gerenciamento de acesso root baseado em kernel.
- Sistema de módulos baseado em [Magic Mount](https://topjohnwu.github.io/Magisk/details.html#magic-mount) e [OverlayFS](https://en.wikipedia.org/wiki/OverlayFS).
- [Perfil do app](https://kernelsu.org/pt_BR/guide/app-profile.html): Limitar privilégios root por app.

---

## ✅ Compatibilidade

O KernelSU Next oferece suporte a kernels Android **4.4 até 6.6**:

| Versão do kernel     | Notas de suporte                                                              |
|----------------------|-------------------------------------------------------------------------------|
| 5.10+ (GKI 2.0)      | Suporta imagens pré-compiladas e LKM/KMI                                      |
| 4.19 – 5.4 (GKI 1.0) | Requer driver do KernelSU integrado                                           |
| < 4.14 (EOL)         | Requer driver do KernelSU (3.18+ é experimental e pode precisar de backports) |

**Arquiteturas suportadas:** `arm64-v8a`, `armeabi-v7a` e `x86_64`

---

## 📦 Instalação

Consulte o guia de [Instalação](https://kernelsu-next.github.io/webpage/pt_BR/pages/installation.html) para obter instruções de configuração.

---

## 🔐 Segurança

Para relatar problemas de segurança, consulte [SECURITY.md](/SECURITY.md).

---

## 📜 Licença

- **Diretório `/kernel`:** [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).
- **Todos os outros arquivos:** [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html).

---

## 💸 Doações

Se você quiser apoiar o projeto:

- **USDT (BEP20, ERC20)**: `0x12b5224b7aca0121c2f003240a901e1d064371c1`
- **USDT (TRC20)**: `TYUVMWGTcnR5svnDoX85DWHyqUAeyQcdjh`
- **ETH (ERC20)**: `0x12b5224b7aca0121c2f003240a901e1d064371c1`
- **LTC**: `Ld238uYBuRQdZB5YwdbkuU6ektBAAUByoL`
- **BTC**: `19QgifcjMjSr1wB2DJcea5cxitvWVcXMT6`

---

## 🙏 Créditos

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/) – Inspiração do conceito
- [Magisk](https://github.com/topjohnwu/Magisk) – Implementação root principal
- [Genuine](https://github.com/brevent/genuine/) – Validação de assinatura APK v2
- [Diamorphine](https://github.com/m0nad/Diamorphine) – Técnicas de rootkit
- [KernelSU](https://github.com/tiann/KernelSU) – A base original que tornou o KernelSU Next possível
- [Magic Mount Port](https://github.com/5ec1cff/KernelSU/blob/main/userspace/ksud/src/magic_mount.rs) – 💜 para 5ec1cff por manter o KernelSU vivo
