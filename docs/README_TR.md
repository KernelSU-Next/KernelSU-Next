[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | **Türkçe** | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [Українська](README_UA.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | [日本語](README_JA.md)

---

<div align="center">
  <img src="/assets/kernelsu_next.png" width="96" alt="KernelSU Next Logosu">

  <h2>KernelSU Next</h2>
  <p><strong>Android cihazlar için çekirdek tabanlı bir root çözümüdür.</strong></p>

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

## 🚀 Özellikler

- Çekirdek tabanlı `su` ve root erişim yönetimi.
- **[Magic Mount](https://topjohnwu.github.io/Magisk/details.html#magic-mount)** ve **[OverlayFS](https://en.wikipedia.org/wiki/OverlayFS)** tabanlı modül sistemi.
- [Uygulama Profili](https://kernelsu.org/guide/app-profile.html): Uygulama başına root yetkisini sınırlandırma.

---

## ✅ Uyumluluk

KernelSU Next, **4.4 ile 6.6** arasındaki Android çekirdeklerini destekler:

| Çekirdek Sürümü       | Destek Notları                                                           |
|------------------------|--------------------------------------------------------------------------|
| 5.10+ (GKI 2.0)        | Hazır imajlar ve LKM/KMI desteği                                        |
| 4.19 – 5.4 (GKI 1.0)   | KernelSU sürücüsünün çekirdeğe gömülü olması gerekir                    |
| < 4.14 (EOL)           | KernelSU sürücüsü gerekir (3.18+ deneysel olup yama gerektirebilir)     |

**Desteklenen mimariler:** `arm64-v8a`, `armeabi-v7a`, `x86_64`

---

## 📦 Kurulum

Kurulum talimatları için [Kurulum Kılavuzu](https://kernelsu-next.github.io/webpage/pages/installation.html) sayfasına bakınız.

---

## 🔐 Güvenlik

Güvenlik açıklarını bildirmek için lütfen [SECURITY.md](/SECURITY.md) dosyasına bakınız.

---

## 📜 Lisans

- **`/kernel` dizini:** [Yalnızca GPL-2.0](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
- **Diğer tüm dosyalar:** [GPL-3.0-veya-sonrası](https://www.gnu.org/licenses/gpl-3.0.html)

---

## 💸 Bağışlar

Projeye destek olmak isterseniz:

- **USDT (BEP20, ERC20):** `0x12b5224b7aca0121c2f003240a901e1d064371c1`
- **USDT (TRC20):** `TYUVMWGTcnR5svnDoX85DWHyqUAeyQcdjh`
- **ETH (ERC20):** `0x12b5224b7aca0121c2f003240a901e1d064371c1`
- **LTC:** `Ld238uYBuRQdZB5YwdbkuU6ektBAAUByoL`
- **BTC:** `19QgifcjMjSr1wB2DJcea5cxitvWVcXMT6`

---

## 🙏 Katkıda Bulunanlar

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/) – KernelSU Fikrinin temeli
- [Magisk](https://github.com/topjohnwu/Magisk) – Temel root altyapısı
- [Genuine](https://github.com/brevent/genuine/) – APK v2 imza doğrulaması
- [Diamorphine](https://github.com/m0nad/Diamorphine) – Rootkit teknikleri
- [KernelSU](https://github.com/tiann/KernelSU) – KernelSU Next'in temelini oluşturan orijinal proje
- [Magic Mount Port](https://github.com/5ec1cff/KernelSU/blob/main/userspace/ksud/src/magic_mount.rs) – KernelSU’yu kurtardığı için 💜 5ec1cff’e teşekkürler
