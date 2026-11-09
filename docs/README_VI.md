[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | [한국어](README_KO.md) | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [Українська](README_UA.md) | [ภาษาไทย](README_TH.md) | **Tiếng Việt** | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | [日本語](README_JA.md) | [Español](README_ES.md)

---

<div align="center">
  <img src="/assets/kernelsu_next.png" width="96" alt="KernelSU Next Logo">

  <h2>KernelSU Next</h2>
  <p><strong>Giải pháp root dựa trên kernel cho thiết bị Android.</strong></p>

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

## 🚀 Tính năng

- Quản lý quyền root và `su` dựa trên kernel.
- Hệ thống mô-đun dựa trên [metamodules](https://kernelsu.org/guide/metamodule.html): hạ tầng có thể cắm thêm cho các thay đổi không cần thay đổi hệ thống.
- Tải driver SUSFS LKM tích hợp sẵn cho thiết bị GKI 2.0.
- Phát hiện rủi ro chặn cài đặt các mô-đun đáng ngờ.
- Hỗ trợ sao lưu và khôi phục mô-đun và hồ sơ ứng dụng.
- [Hồ sơ ứng dụng](https://kernelsu.org/guide/app-profile.html): khóa quyền root trong một chiếc lồng.

---

## ✅ Tương thích

KernelSU Next hỗ trợ các kernel Android từ **4.4 đến 6.18**.

| Phiên bản kernel | Ghi chú hỗ trợ |
|----------------------|-------------------------------------------------------------------------|
| 5.10+ (GKI 2.0)      | Hỗ trợ hình ảnh đã biên dịch sẵn và LKM/KMI |
| 4.19 – 5.4 (GKI 1.0) | Yêu cầu trình điều khiển KernelSU được nhúng vào kernel |
| < 4.14 (EOL)         | Yêu cầu trình điều khiển KernelSU (3.18+ là bản thử nghiệm và có thể cần backport) |

**Kiến trúc được hỗ trợ:** `arm64-v8a` và `x86_64`

> [!CAUTION]
> Các phiên bản kernel gần đây đã áp dụng một thay đổi phá vỡ khiến KernelSU Next thất bại và có thể kích hoạt kernel panic trên `x86_64`! Hãy kiểm tra trang web để biết thêm thông tin!

---

## 📦 Cài đặt

Vui lòng tham khảo hướng dẫn [Cài đặt](https://kernelsu-next.github.io/webpage/pages/installation.html) để biết hướng dẫn thiết lập.

---

## 🏅 Đóng góp

- Truy cập [Crowdin](https://crowdin.com/project/kernelsu-next) để gửi bản dịch cho trình quản lý!
- Để báo cáo các vấn đề bảo mật, vui lòng xem [SECURITY.md](/SECURITY.md).

---

## 📜 Giấy phép

- **thư mục `/kernel`:** [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).
- **Tất cả các tệp khác:** [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html).

---

## 💸 Quyên góp

Nếu bạn muốn hỗ trợ dự án:

- **USDT (BEP20, ERC20)**: `0x0b269716dc00692676e4217e23c7f67abd5f0f3e`
- **USDT (TRC20)**: `TXu6VX8dvJLQPx4WBobjbYT5tefHTDikWo`
- **LTC**: `LMYRqRXPKAFpYncBhNS44W1iwFit14LH1u`

---

## 🙏 Ghi nhận

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/) – Cảm hứng về khái niệm
- [Magisk](https://github.com/topjohnwu/Magisk) – Triển khai root cốt lõi
- [Genuine](https://github.com/brevent/genuine/) – Xác thực chữ ký APK v2
- [Diamorphine](https://github.com/m0nad/Diamorphine) – Kỹ thuật rootkit
- [KernelSU](https://github.com/tiann/KernelSU) – Nền tảng gốc khiến KernelSU Next trở nên khả thi
- [Susfs4ksu](https://gitlab.com/simonpunk/susfs4ksu) – Tiện ích ẩn root ở cấp kernel cho KernelSU
- [Susfs4ksu-lkm](https://github.com/inforcqb/susfs4ksu-lkm) – SUSFS được port sang dạng mô-đun kernel có thể tải

