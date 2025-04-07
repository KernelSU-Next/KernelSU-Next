[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | **Azərbaycanca** | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md)

# KernelSU Next

<img src="/assets/kernelsu_next.png" style="width: 96px;" alt="logo">

Android cihazlar üçün nüvə təməlli bir root həlli.

[![Latest Release](https://img.shields.io/github/v/release/KernelSU-Next/KernelSU-Next?label=Release&logo=github)](https://github.com/KernelSU-Next/KernelSU-Next/releases/latest)
[![Nightly Release](https://img.shields.io/badge/Nightly%20Release-gray?logo=hackthebox&logoColor=fff)](https://nightly.link/KernelSU-Next/KernelSU-Next/workflows/build-manager-ci/next/Manager)
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-orange.svg?logo=gnu)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![GitHub License](https://img.shields.io/github/license/KernelSU-Next/KernelSU-Next?logo=gnu)](/LICENSE)

## Özəllikler

1. Nüvə təməlli `su` ve kök icazə yönətimi.
2. Dinamik montaj sisteminə əsaslanan modül sistemi [Magic Mount](https://topjohnwu.github.io/Magisk/details.html#magic-mount) / [OverlayFS](https://en.wikipedia.org/wiki/OverlayFS).
3. [App Profile](https://kernelsu.org/guide/app-profile.html): Kök gücünü bir qəfəsə kilidləyin.

## Uyğunluq Vəziyyəti

KernelSU Next, 4.4-dən başlayaraq 6.6-ya qədər əksər Android nüvələrini rəsmi olaraq dəstəkləyir.
 - GKI 2.0 (5.10+) nüvələri əvvəlcədən yaradılmış görüntüləri və LKM/KMI-ni işlədə bilər.
 - GKI 1.0 (4.19 - 5.4) nüvələrinin KernelSU driveri ilə yenidən yaradılması lazımdır.
 - EOL (<4.14) nüvələri də KernelSU driveri ilə yenidən yaradılmalıdır. (3.18+ eksperimentaldır və bəzi funksiyalar bərpa edilməsinə ehtiyac duya bilər.)

Hazırda yalnız `arm64-v8a` dəstəklənir.

## Istifadə

- [Quraşdırma Təlimatı](https://KernelSU-Next.github.io/KernelSU-Next/)

## Təhlükəsizlik

KernelSU-da təhlükəsizlik boşluqlarını bildirmək haqqında məlumat üçün [SECURITY.md](/SECURITY.md) bölməsinə baxın.

## Lisenziya

- `kernel` qovluğu altındakı fayllar yalnız [GPL-2.0](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html) lisenziyası ilə paylanır.
- `kernel` qovluğundan kənardakı bütün digər hissələr [GPL-3.0](https://www.gnu.org/licenses/gpl-3.0.html) və ya daha sonrakı versiya lisenziyası ilə paylanır.

## Təşəkkürlər

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/): KernelSU ideyasının ilham mənbəyi.
- [Magisk](https://github.com/topjohnwu/Magisk): Güclü root vasitəsi.
- [genuine](https://github.com/brevent/genuine/): APK v2 imza doğrulaması.
- [Diamorphine](https://github.com/m0nad/Diamorphine): Bəzi rootkit bacarıqları üçün.
- [KernelSU](https://github.com/tiann/KernelSU): tiann'a təşəkkürlər, çünki o olmasaydı KernelSU Next də olmazdı.
- [Magic Mount Port](https://github.com/5ec1cff/KernelSU/blob/main/userspace/ksud/src/magic_mount.rs): 💜 KernelSU-nu xilas etdiyin üçün təşəkkürlər, 5ec1cff!
