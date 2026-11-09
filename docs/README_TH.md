[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [Українська](README_UA.md) | **ภาษาไทย** | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | [日本語](README_JA.md) | [Español](README_ES.md)

---

<div align="center">
  <img src="/assets/kernelsu_next.png" width="96" alt="KernelSU Next Logo">

  <h2>KernelSU Next</h2>
  <p><strong>โซลูชันรูทบนพื้นฐานเคอร์เนลสำหรับอุปกรณ์ Android</strong></p>

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

## 🚀 คุณสมบัติ

- จัดการการเข้าถึงรูทและ `su` บนพื้นฐานเคอร์เนล
- ระบบโมดูลตาม [metamodules](https://kernelsu.org/guide/metamodule.html): โครงสร้างที่สามารถเสียบปลั๊กได้สำหรับการปรับเปลี่ยนแบบไม่แทรกแซงระบบ
- การโหลดไดรเวอร์ SUSFS LKM ในตัวสำหรับอุปกรณ์ GKI 2.0
- การตรวจจับความเสี่ยงที่ป้องกันการติดตั้งโมดูลที่น่าสงสัย
- รองรับการสำรองข้อมูลและกู้คืนโมดูลและโปรไฟล์แอป
- [App Profile](https://kernelsu.org/guide/app-profile.html): จำกัดสิทธิ์ของรูทไว้ในกรง

---

## ✅ ความเข้ากันได้

KernelSU Next รองรับเคอร์เนล Android ตั้งแต่ **4.4 ถึง 6.18**

| เวอร์ชันเคอร์เนล | หมายเหตุการรองรับ |
|----------------------|-------------------------------------------------------------------------|
| 5.10+ (GKI 2.0)      | รองรับภาพที่สร้างไว้ล่วงหน้าและ LKM/KMI |
| 4.19 – 5.4 (GKI 1.0) | ต้องมีไดรเวอร์ KernelSU ที่ฝังอยู่ในเคอร์เนล |
| < 4.14 (EOL)         | ต้องมีไดรเวอร์ KernelSU (3.18+ ยังเป็นเวอร์ชันทดลอง และอาจต้องมี backport) |

**สถาปัตยกรรมที่รองรับ:** `arm64-v8a` และ `x86_64`

> [!CAUTION]
> เวอร์ชันเคอร์เนลล่าสุดได้มีการเปลี่ยนแปลงที่ทำให้ KernelSU Next ล้มเหลว และอาจก่อให้เกิด kernel panic บน `x86_64`! กรุณาตรวจสอบเว็บไซต์เพื่อข้อมูลเพิ่มเติม!

---

## 📦 การติดตั้ง

โปรดดู [คู่มือการติดตั้ง](https://kernelsu-next.github.io/webpage/pages/installation.html) สำหรับคำแนะนำการตั้งค่า

---

## 🏅 การมีส่วนร่วม

- ไปที่ [Crowdin](https://crowdin.com/project/kernelsu-next) เพื่อส่งการแปลสำหรับตัวจัดการ!
- หากต้องการรายงานปัญหาด้านความปลอดภัย โปรดอ่าน [SECURITY.md](/SECURITY.md)

---

## 📜 สัญญาอนุญาต

- **ไดเรกทอรี `/kernel`:** [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).
- **ไฟล์อื่น ๆ ทั้งหมด:** [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html).

---

## 💸 การบริจาค

หากคุณต้องการสนับสนุนโครงการ:

- **USDT (BEP20, ERC20)**: `0x0b269716dc00692676e4217e23c7f67abd5f0f3e`
- **USDT (TRC20)**: `TXu6VX8dvJLQPx4WBobjbYT5tefHTDikWo`
- **LTC**: `LMYRqRXPKAFpYncBhNS44W1iwFit14LH1u`

---

## 🙏 เครดิต

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/) – แรงบันดาลใจแนวคิด
- [Magisk](https://github.com/topjohnwu/Magisk) – การใช้งาน root หลัก
- [Genuine](https://github.com/brevent/genuine/) – การตรวจสอบลายเซ็น APK v2
- [Diamorphine](https://github.com/m0nad/Diamorphine) – เทคนิค rootkit
- [KernelSU](https://github.com/tiann/KernelSU) – ฐานดั้งเดิมที่ทำให้ KernelSU Next เกิดขึ้นได้
- [Susfs4ksu](https://gitlab.com/simonpunk/susfs4ksu) – ส่วนขยายการปกปิด root ระดับเคอร์เนลสำหรับ KernelSU
- [Susfs4ksu-lkm](https://github.com/inforcqb/susfs4ksu-lkm) – SUSFS ที่ถูก port เป็นโมดูลเคอร์เนลแบบโหลดได้

