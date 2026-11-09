[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | **Português (Brasil)** | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [Українська](README_UA.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | [日本語](README_JA.md) | [Español](README_ES.md)

---

<div align="center">
  <img src="/assets/kernelsu_next.png" width="96" alt="KernelSU Next Logo">

  <h2>KernelSU Next</h2>
  <p><strong>Uma solução de root baseada em kernel para dispositivos Android.</strong></p>

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

## 🚀 Recursos

- Gerenciamento de acesso root e `su` baseado em kernel.
- Sistema de módulos baseado em [metamodules](https://kernelsu.org/guide/metamodule.html): infraestrutura plugável para modificações sem sistema.
- Carregamento integrado do driver SUSFS LKM para dispositivos GKI 2.0.
- Detecção de risco que bloqueia a instalação de módulos suspeitos.
- Suporte para backup e restauração de módulos e perfis de aplicativos.
- [Perfil do aplicativo](https://kernelsu.org/guide/app-profile.html): prende o poder do root em uma jaula.

---

## ✅ Compatibilidade

O KernelSU Next suporta kernels Android de **4.4 até 6.18**.

| Versão do kernel | Notas de suporte |
|----------------------|-------------------------------------------------------------------------|
| 5.10+ (GKI 2.0)      | Suporta imagens pré-compiladas e LKM/KMI |
| 4.19 – 5.4 (GKI 1.0) | Requer o driver KernelSU embutido no kernel |
| < 4.14 (EOL)         | Requer o driver KernelSU (3.18+ é experimental e pode exigir backports) |

**Arquiteturas suportadas:** `arm64-v8a` e `x86_64`

> [!CAUTION]
> Versões recentes do kernel implementaram uma mudança quebradora que faz o KernelSU Next falhar e potencialmente disparar um kernel panic em `x86_64`! Verifique o site para obter mais informações!

---

## 📦 Instalação

Consulte o guia de [Instalação](https://kernelsu-next.github.io/webpage/pages/installation.html) para obter instruções de configuração.

---

## 🏅 Contribuição

- Acesse nosso [Crowdin](https://crowdin.com/project/kernelsu-next) para enviar uma tradução para o gerenciador!
- Para relatar problemas de segurança, consulte [SECURITY.md](/SECURITY.md).

---

## 📜 Licença

- **diretório `/kernel`:** [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).
- **Todos os outros arquivos:** [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html).

---

## 💸 Doações

Se você quiser apoiar o projeto:

- **USDT (BEP20, ERC20)**: `0x0b269716dc00692676e4217e23c7f67abd5f0f3e`
- **USDT (TRC20)**: `TXu6VX8dvJLQPx4WBobjbYT5tefHTDikWo`
- **LTC**: `LMYRqRXPKAFpYncBhNS44W1iwFit14LH1u`

---

## 🙏 Créditos

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/) – Inspiração conceitual
- [Magisk](https://github.com/topjohnwu/Magisk) – Implementação central do root
- [Genuine](https://github.com/brevent/genuine/) – Validação de assinatura APK v2
- [Diamorphine](https://github.com/m0nad/Diamorphine) – Técnicas de rootkit
- [KernelSU](https://github.com/tiann/KernelSU) – A base original que tornou o KernelSU Next possível
- [Susfs4ksu](https://gitlab.com/simonpunk/susfs4ksu) – Extensão de ocultação do root em nível de kernel para KernelSU
- [Susfs4ksu-lkm](https://github.com/inforcqb/susfs4ksu-lkm) – SUSFS portado como módulo de kernel carregável

