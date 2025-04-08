[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | **ภาษาไทย** | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | [Magyar](README_HU.md)

# KernelSU Next

<img src="/assets/kernelsu_next.png" style="width: 96px;" alt="logo">

โซลูชั่นรูทบนพื้นฐานเคอร์เนลสำหรับอุปกรณ์ Android

[![Latest Release](https://img.shields.io/github/v/release/KernelSU-Next/KernelSU-Next?label=Release&logo=github)](https://github.com/KernelSU-Next/KernelSU-Next/releases/latest)
[![Nightly Release](https://img.shields.io/badge/Nightly%20Release-gray?logo=hackthebox&logoColor=fff)](https://nightly.link/KernelSU-Next/KernelSU-Next/workflows/build-manager-ci/next/Manager)
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-orange.svg?logo=gnu)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![GitHub License](https://img.shields.io/github/license/KernelSU-Next/KernelSU-Next?logo=gnu)](/LICENSE)

## คุณสมบัติ

1. จัดการการเข้าถึงรูท และ `su` บนพื้นฐานเคอร์เนล
2. ระบบโมดูลแบบไดนามิกเมานต์ [Magic Mount](https://topjohnwu.github.io/Magisk/details.html#magic-mount) / [OverlayFS](https://en.wikipedia.org/wiki/OverlayFS)
3. [App Profile](https://kernelsu.org/guide/app-profile.html): จำกัดสิทธิ์เข้าถึงรูทสำหรับแอปต่างๆ

## การเข้ากันในอุปกรณ์ต่างๆ

KernelSU Next รองรับแบบเป็นทางการตั้งแต่เคอร์เนลแอนดรอยด์ 4.4 ถึง 6.6
 - GKI 2.0 (5.10+) เคอร์เนลสามารถรันไฟล์อิมเมจสำเร็จรูป และ LKM/KMI ได้
 - GKI 1.0 (4.19 - 5.4) เคอร์เนลจะต้องรีบิ้วร่วมกับไดร์เวอร์ของ KernelSU
 - EOL (<4.14) เคอร์เนลก็ต้องรีบิ้วร่วมกับไดร์เวอร์ของ KernelSU เช่นกัน (3.18+ ยังเป็นเวอร์ชั่นทดลอง และยังต้องเขียนฟังก์ชั่นหลังบ้านเพิ่มเติม)

ในขณะนี้, มีแค่สถาปัตยกรรม `arm64-v8a` ที่รองรับเท่านั้น

## การใช้งาน

- [คำแนะนำในการติดตั้ง](https://KernelSU-Next.github.io/KernelSU-Next/)

## ความปลอดภัย

สำหรับข้อมูลการรายงานช่องโหว่ด้านความปลอดภัยของ KernelSU โปรดดูที่ [SECURITY.md](/SECURITY.md).

## สัญญาอนุญาต

- ไฟล์ภายใต้โฟลเดอร์ `kernel` ถือว่าเป็นสัญญาอนุญาต [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).
- ไฟล์ที่นอกเหนือจากโฟลเดอร์ `kernel` ถือว่าเป็นสัญญาอนุญาต [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html).

## การบริจาก

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ USDT BEP20 ]

- TYUVMWGTcnR5svnDoX85DWHyqUAeyQcdjh [ USDT TRC20 ]

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ USDT ERC20 ]

- 0x12b5224b7aca0121c2f003240a901e1d064371c1 [ ETH ERC20 ]

- Ld238uYBuRQdZB5YwdbkuU6ektBAAUByoL [ LTC ]

- 19QgifcjMjSr1wB2DJcea5cxitvWVcXMT6 [ BTC ]

## เครดิต

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/): ที่เป็นคนริเริ่มไอเดีย KernelSU
- [Magisk](https://github.com/topjohnwu/Magisk): เครื่องมือรูทที่ทรงพลัง
- [genuine](https://github.com/brevent/genuine/): การตรวจสอบลายเซ็น APK v2
- [Diamorphine](https://github.com/m0nad/Diamorphine): ความรู้เกี่ยวกับ rootkit
- [KernelSU](https://github.com/tiann/KernelSU): ต้องขอบคุณ tiann ไม่งั้นจะไม่มี KernelSU ขึ้นมา
- [Magic Mount Port](https://github.com/5ec1cff/KernelSU/blob/main/userspace/ksud/src/magic_mount.rs): 💜 5ec1cff ที่ช่วย KernelSU เอาไว้!
