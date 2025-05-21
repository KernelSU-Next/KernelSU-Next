[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | **Türkçe** | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | [日本語](README_JA.md)

# KernelSU Next

<img src="/assets/kernelsu_next.png" style="width: 96px;" alt="logo">

Android cihazlar için çekirdek tabanlı root çözümü.

[![Son Sürüm](https://img.shields.io/github/v/release/KernelSU-Next/KernelSU-Next?label=Release&logo=github)](https://github.com/KernelSU-Next/KernelSU-Next/releases/latest)  
[![Gece Sürümü](https://img.shields.io/badge/Nightly%20Release-gray?logo=hackthebox&logoColor=fff)](https://nightly.link/KernelSU-Next/KernelSU-Next/workflows/build-manager-ci/next/Manager)  
[![Lisans: GPL v2](https://img.shields.io/badge/License-GPL%20v2-orange.svg?logo=gnu)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)  
[![GitHub Lisansı](https://img.shields.io/github/license/KernelSU-Next/KernelSU-Next?logo=gnu)](/LICENSE)

## Özellikler

1. Çekirdek tabanlı `su` ve root erişimi yönetimi.  
2. Dinamik bağlama sistemi [Magic Mount](https://topjohnwu.github.io/Magisk/details.html#magic-mount) / [OverlayFS](https://en.wikipedia.org/wiki/OverlayFS) tabanlı modül sistemi.  
3. [Uygulama Profili](https://kernelsu.org/guide/app-profile.html): Root yetkisini bir kafese kilitleyin.

## Uyumluluk Durumu

KernelSU Next, resmi olarak Android çekirdeklerinin çoğunu 4.4 sürümünden 6.6 sürümüne kadar destekler.
 - GKI 2.0 (5.10+) çekirdekleri, hazır imajları ve LKM/KMI desteğini çalıştırabilir.
 - GKI 1.0 (4.19 - 5.4) çekirdeklerinin KernelSU sürücüsü ile yeniden derlenmesi gerekir.
 - EOL (<4.14) çekirdekler de KernelSU sürücüsüyle yeniden derlenmelidir (3.18+ deneysel olup bazı fonksiyonların geri aktarımı gerekebilir).

Şu anda yalnızca `arm64-v8a` mimarisi desteklenmektedir.

## Kullanım

- [Kurulum Talimatları](https://ksunext.org/pages/installation.html)

## Güvenlik

KernelSU'daki güvenlik açıklarını bildirme hakkında bilgi için bkz: [SECURITY.md](/SECURITY.md)

## Lisans

- `kernel` dizinindeki dosyalar [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html) lisanslıdır.  
- `kernel` dizini dışındaki tüm diğer bölümler [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html) lisansı altındadır.

## Bağışlar

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ USDT BEP20 ]

- TYUVMWGTcnR5svnDoX85DWHyqUAeyQcdjh [ USDT TRC20 ]

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ USDT ERC20 ]

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ ETH ERC20 ]

- Ld238uYBuRQdZB5YwdbkuU6ektBAAUByoL [ LTC ]

- 19QgifcjMjSr1wB2DJcea5cxitvWVcXMT6 [ BTC ]

## Katkıda Bulunanlar

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/): KernelSU fikrinin temeli.  
- [Magisk](https://github.com/topjohnwu/Magisk): Güçlü root aracı.  
- [genuine](https://github.com/brevent/genuine/): APK v2 imza doğrulama.  
- [Diamorphine](https://github.com/m0nad/Diamorphine): Bazı rootkit teknikleri.  
- [KernelSU](https://github.com/tiann/KernelSU): tiann'a teşekkürler, KernelSU Next onun sayesinde var.  
- [Magic Mount Port](https://github.com/5ec1cff/KernelSU/blob/main/userspace/ksud/src/magic_mount.rs): 💜 KernelSU'yu kurtardığı için 5ec1cff'e teşekkürler!