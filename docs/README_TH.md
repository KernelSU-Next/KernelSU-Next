[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | **ภาษาไทย** | [Tiếng Việt](README_VI.md)

# KernelSU Next

<img src="/assets/kernelsu_next.png" style="width: 96px;" alt="logo">

โซลูชันรูทบนพื้นฐานเคอร์เนลสำหรับอุปกรณ์ Android

[![Latest Release](https://img.shields.io/github/v/release/KernelSU-Next/KernelSU-Next?label=Release&logo=github)](https://github.com/KernelSU-Next/KernelSU-Next/releases/latest)
[![Nightly Release](https://img.shields.io/badge/Nightly%20Release-gray?logo=hackthebox&logoColor=fff)](https://nightly.link/KernelSU-Next/KernelSU-Next/workflows/build-manager-ci/next/Manager)
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-orange.svg?logo=gnu)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![GitHub License](https://img.shields.io/github/license/KernelSU-Next/KernelSU-Next?logo=gnu)](/LICENSE)

## คุณสมบัติ

1. การจัดการการเข้าถึงรูทและ `su` บนเคอร์เนล
2. ระบบโมดูลที่ใช้ระบบการติดตั้งแบบไดนามิก [Magic Mount](https://topjohnwu.github.io/Magisk/details.html#magic-mount) / [OverlayFS](https://en.wikipedia.org/wiki/OverlayFS)
3. [App Profile](https://kernelsu.org/guide/app-profile.html): จำกัดพลังรูทไว้สำหรับแอปต่างๆ

## การเข้ากันในอุปกรณ์ต่างๆ

KernelSU Next รองรับแบบเป็นทางการตั้งแต่เคอร์เนลแอนดรอยด์ 4.4 ถึง 6.6
 - GKI 2.0 (5.10+) เคอร์เนลสามารถรันแบบไฟล์สำเร็จรูปและ LKM/KMI ได้
 - GKI 1.0 (4.19 - 5.4) เคอร์เนลต้องการ build ร่วมกับไดร์เวอร์ของทาง KernelSU
 - EOL (<4.14) เคอร์เนลก็ต้องการ build ร่วมกับไดร์เวอร์ของทาง KernelSU เช่นกัน (3.18+ ยังเป็นการทดลองอยู่และยังต้องการเขียนในหลังบ้านเพิ่มเติม)

ในขณะนี้, มีแค่สถาปัตยกรรม `arm64-v8a` ที่รองรับเท่านั้น

## การใช้งาน

- [คำแนะนำในการติดตั้ง](https://KernelSU-Next.github.io/KernelSU-Next/)

## ความปลอดภัย

สำหรับข้อมูลเกี่ยวกับการรายงานช่องโหว่ด้านความปลอดภัยใน KernelSU โปรดดูที่ [SECURITY.md](/SECURITY.md).

## ใบอนุญาต

- ไฟล์ที่ภายใต้โฟลเดอร์ `kernel` ถือว่าเป็น [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).
- ไฟล์ที่นอกเหนือจากโฟลเดอร์ `kernel` ถือว่าเป็น [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html).

## เครดิต

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/): ที่เป็นคนริเริ่มไอเดียเกี่ยวกับ KernelSU
- [Magisk](https://github.com/topjohnwu/Magisk): อุปกรณ์มือเกี่ยวกับรูทที่ทรงพลัง
- [genuine](https://github.com/brevent/genuine/): การออกลายเซ็นให้กับไฟล์ APK v2
- [Diamorphine](https://github.com/m0nad/Diamorphine): ความรู้ความสามารถเกี่ยวกับ rootkit
- [KernelSU](https://github.com/tiann/KernelSU): ต้องขอบคุณ tiann ถ้าไม่มีคนนั้นก็ไม่มีสิ่งที่เรียกว่า KernelSU เกิดขึ้น
- [Magic Mount Port](https://github.com/5ec1cff/KernelSU/blob/main/userspace/ksud/src/magic_mount.rs): 💜 5ec1cff ที่ช่วย KernelSU ไว้
