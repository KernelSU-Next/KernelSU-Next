[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [Українська](README_UA.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | **日本語** | [Español](README_ES.md)

---

<div align="center">
  <img src="/assets/kernelsu_next.png" width="96" alt="KernelSU Next Logo">

  <h2>KernelSU Next</h2>
  <p><strong>Android デバイス向けのカーネルベースの root ソリューション。</strong></p>

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

## 🚀 機能

- カーネルベースの `su` と root アクセス管理。
- [metamodules](https://kernelsu.org/guide/metamodule.html) をベースにしたモジュールシステム: システムレス修正のための拡張可能基盤。
- GKI 2.0 デバイス向けの組み込み SUSFS LKM ドライバ読み込み。
- 不審なモジュールのインストールをブロックするリスク検出。
- モジュールとアプリ プロファイルのバックアップと復元をサポート。
- [アプリプロファイル](https://kernelsu.org/guide/app-profile.html): root 権限をケージに閉じ込めます。

---

## ✅ 互換性

KernelSU Next は **4.4 から 6.18** までの Android カーネルをサポートしています。

| カーネルバージョン | サポートノート |
|----------------------|-------------------------------------------------------------------------|
| 5.10+ (GKI 2.0)      | 事前ビルド済みイメージおよび LKM/KMI をサポート |
| 4.19 – 5.4 (GKI 1.0) | KernelSU ドライバがカーネルに組み込まれている必要があります |
| < 4.14 (EOL)         | KernelSU ドライバが必要です（3.18+ は実験的で、バックポートが必要な場合があります） |

**対応アーキテクチャ:** `arm64-v8a` と `x86_64`

> [!CAUTION]
> 新しいカーネルバージョンでは破壊的な変更が導入されており、KernelSU Next が失敗し、`x86_64` 上で kernel panic を引き起こす可能性があります！詳細は公式サイトをご確認ください。

---

## 📦 インストール

セットアップ手順については [インストール](https://kernelsu-next.github.io/webpage/pages/installation.html) を参照してください。

---

## 🏅 貢献

- マネージャー向けの翻訳を送信するには [Crowdin](https://crowdin.com/project/kernelsu-next) をご利用ください。
- セキュリティ問題を報告するには [SECURITY.md](/SECURITY.md) を確認してください。

---

## 📜 ライセンス

- **`/kernel` ディレクトリ:** [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)。
- **その他のすべてのファイル:** [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html)。

---

## 💸 寄付

プロジェクトを支援したい場合：

- **USDT (BEP20, ERC20)**: `0x0b269716dc00692676e4217e23c7f67abd5f0f3e`
- **USDT (TRC20)**: `TXu6VX8dvJLQPx4WBobjbYT5tefHTDikWo`
- **LTC**: `LMYRqRXPKAFpYncBhNS44W1iwFit14LH1u`

---

## 🙏 クレジット

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/) – コンセプトの着想
- [Magisk](https://github.com/topjohnwu/Magisk) – コア root 実装
- [Genuine](https://github.com/brevent/genuine/) – APK v2 署名検証
- [Diamorphine](https://github.com/m0nad/Diamorphine) – Rootkit 技術
- [KernelSU](https://github.com/tiann/KernelSU) – KernelSU Next を可能にした元の基盤
- [Susfs4ksu](https://gitlab.com/simonpunk/susfs4ksu) – KernelSU 向けカーネルレベルの root 隠蔽拡張
- [Susfs4ksu-lkm](https://github.com/inforcqb/susfs4ksu-lkm) – 読み込み可能なカーネルモジュールとして移植された SUSFS

