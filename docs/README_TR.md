[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | **Türkçe** | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [Українська](README_UA.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | [日本語](README_JA.md) | [Español](README_ES.md)

---

<div align="center">
  <img src="/assets/kernelsu_next.png" width="96" alt="KernelSU Next Logo">

  <h2>KernelSU Next</h2>
  <p><strong>Android cihazlar için çekirdek tabanlı bir root çözümüdür.</strong></p>

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

## 🚀 Özellikler

- Çekirdek tabanlı `su` ve root erişim yönetimi.
- [metamodules](https://kernelsu.org/guide/metamodule.html) tabanlı modül sistemi: sistem dışı değişiklikler için eklenebilir altyapı.
- GKI 2.0 cihazlarda dahili SUSFS LKM sürücü yükleme desteği.
- Şüpheli modüllerin kurulmasını engelleyen risk algılama.
- Modüller ve uygulama profilleri için yedekleme ve geri yükleme desteği.
- [Uygulama Profili](https://kernelsu.org/guide/app-profile.html): root gücünü bir kafese kapatır.

---

## ✅ Uyumluluk

KernelSU Next, **4.4 ile 6.18** arasındaki Android çekirdeklerini destekler.

| Çekirdek sürümü | Destek notları |
|----------------------|-------------------------------------------------------------------------|
| 5.10+ (GKI 2.0)      | Önceden derlenmiş imajlar ve LKM/KMI desteği |
| 4.19 – 5.4 (GKI 1.0) | KernelSU sürücüsünün çekirdeğe gömülü olması gerekir |
| < 4.14 (EOL)         | KernelSU sürücüsü gerekir (3.18+ deneysel olup geri yama gerekebilir) |

**Desteklenen mimariler:** `arm64-v8a` ve `x86_64`

> [!CAUTION]
> Yeni çekirdek sürümleri, KernelSU Next'in çalışmasını bozacak ve `x86_64` üzerinde kernel panic tetikleyebilecek kırıcı bir değişiklik uyguladı! Daha fazla bilgi için web sitesini kontrol edin!

---

## 📦 Kurulum

Kurulum talimatları için [Kurulum Kılavuzu](https://kernelsu-next.github.io/webpage/pages/installation.html) sayfasına bakınız.

---

## 🏅 Katkı

- Yönetici için çeviri göndermek için [Crowdin](https://crowdin.com/project/kernelsu-next) adresine gidin!
- Güvenlik açıklarını bildirmek için lütfen [SECURITY.md](/SECURITY.md) dosyasına bakınız.

---

## 📜 Lisans

- **`/kernel` dizini:** [Yalnızca GPL-2.0](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).
- **Diğer tüm dosyalar:** [GPL-3.0-veya-sonrası](https://www.gnu.org/licenses/gpl-3.0.html).

---

## 💸 Bağışlar

Projeyi desteklemek isterseniz:

- **USDT (BEP20, ERC20)**: `0x0b269716dc00692676e4217e23c7f67abd5f0f3e`
- **USDT (TRC20)**: `TXu6VX8dvJLQPx4WBobjbYT5tefHTDikWo`
- **LTC**: `LMYRqRXPKAFpYncBhNS44W1iwFit14LH1u`

---

## 🙏 Katkıda Bulunanlar

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/) – Kavramsal ilham
- [Magisk](https://github.com/topjohnwu/Magisk) – Temel root uygulaması
- [Genuine](https://github.com/brevent/genuine/) – APK v2 imza doğrulaması
- [Diamorphine](https://github.com/m0nad/Diamorphine) – Rootkit teknikleri
- [KernelSU](https://github.com/tiann/KernelSU) – KernelSU Next'in mümkün olmasını sağlayan orijinal temel
- [Susfs4ksu](https://gitlab.com/simonpunk/susfs4ksu) – KernelSU için çekirdek seviyesinde root gizleme eklentisi
- [Susfs4ksu-lkm](https://github.com/inforcqb/susfs4ksu-lkm) – Yüklenebilir çekirdek modülü olarak taşınan SUSFS

