[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [Український](README_UA.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md)  | **日本語**

# KernelSU Next

<img src="/assets/kernelsu_next.png" style="width: 96px;" alt="logo">

Android デバイス用のカーネルベースな root ソリューション。

[![Latest Release](https://img.shields.io/github/v/release/KernelSU-Next/KernelSU-Next?label=Release&logo=github)](https://github.com/KernelSU-Next/KernelSU-Next/releases/latest)
[![Nightly Release](https://img.shields.io/badge/Nightly%20Release-gray?logo=hackthebox&logoColor=fff)](https://nightly.link/KernelSU-Next/KernelSU-Next/workflows/build-manager-ci/next/Manager)
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-orange.svg?logo=gnu)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![GitHub License](https://img.shields.io/github/license/KernelSU-Next/KernelSU-Next?logo=gnu)](/LICENSE)

## 機能

1. カーネルベースの `su` および root アクセスの管理。
2. 動的マウントシステム [Magic Mount](https://topjohnwu.github.io/Magisk/details.html#magic-mount) / [OverlayFS](https://en.wikipedia.org/wiki/OverlayFS) をベースとしたモジュールシステム。
3. [アプリプロファイル](https://kernelsu.org/guide/app-profile.html): root 権限をケージに閉じ込めます。

## 互換性の状態

KernelSU Next は 4.4 から 6.6 までのほとんどの Android カーネルを公式でサポートしています。
 - GKI 2.0 (5.10 以降) のカーネルはビルド済みイメージで LKM/KMI を実行できます。
 - GKI 1.0 (4.19 - 5.4) のカーネルは、KernelSU ドライバを使用してビルドする必要があります。
 - EOL (4.14 未満) のカーネルも KernelSU ドライバを使用して再ビルドする必要があります (3.18 以降は実験中の段階であり、一部の関数のバックポートが必要になる場合があります)。

現在 `arm64-v8a`, `armeabi-v7a` & `x86_64` アーキテクチャのみをサポートしています。

## 使い方

- [インストール手順](https://ksunext.org/pages/installation.html)

## セキュリティ

KernelSU のセキュリティ脆弱性の報告については [SECURITY.md](/SECURITY.md) を参照してください。

## ライセンス

- `kernel` ディレクトリ内のファイルは [GPL-2.0](https://www.gnu.org/licenses/old-licenses/gpl-2.0.ja.html) のみライセンス下にあります。
- `kernel` ディレクトリを除くその他すべての部分は [GPL-3.0 またはそれ以降](https://www.gnu.org/licenses/gpl-3.0.html) のライセンス下にあります。

## 寄付

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ USDT BEP20 ]

- TYUVMWGTcnR5svnDoX85DWHyqUAeyQcdjh [ USDT TRC20 ]

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ USDT ERC20 ]

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ ETH ERC20 ]

- Ld238uYBuRQdZB5YwdbkuU6ektBAAUByoL [ LTC ]

- 19QgifcjMjSr1wB2DJcea5cxitvWVcXMT6 [ BTC ]

## クレジット

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/): KernelSU のアイデアを考案。
- [Magisk](https://github.com/topjohnwu/Magisk): パワフルな root ツール。
- [genuine](https://github.com/brevent/genuine/): APK v2 署名認証。
- [Diamorphine](https://github.com/m0nad/Diamorphine): いくつかの rootkit スキル。
- [KernelSU](https://github.com/tiann/KernelSU): tiann に感謝を申し上げます。これが存在しなければ KernelSU Next は存在しませんでした。
- [Magic Mount Port](https://github.com/5ec1cff/KernelSU/blob/main/userspace/ksud/src/magic_mount.rs): 💜 5ec1cff へ KernelSU を救ってくれてありがとう！
