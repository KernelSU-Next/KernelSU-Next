[English](README.md) | [简体中文](README_CN.md) | [繁體中文](README_TW.md) | [Türkçe](README_TR.md) | [Português (Brasil)](README_PT-BR.md) | **한국어** | [Français](README_FR.md) | [Bahasa Indonesia](README_ID.md) | [Русский](README_RU.md) | [Українська](README_UA.md) | [ภาษาไทย](README_TH.md) | [Tiếng Việt](README_VI.md) | [Italiano](README_IT.md) | [Polski](README_PL.md) | [Български](README_BG.md) | [日本語](README_JA.md) | [Español](README_ES.md)

---

<div align="center">
  <img src="/assets/kernelsu_next.png" width="96" alt="KernelSU Next Logo">

  <h2>KernelSU Next</h2>
  <p><strong>Android 기기를 위한 커널 기반 루트 솔루션입니다.</strong></p>

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

## 🚀 기능

- 커널 기반 `su` 및 루트 접근 관리.
- [metamodules](https://kernelsu.org/guide/metamodule.html) 기반 모듈 시스템: 시스템 없는 수정용 플러그인형 인프라.
- GKI 2.0 기기에서 내장 SUSFS LKM 드라이버 로딩 지원.
- 의심스러운 모듈 설치를 차단하는 위험 감지 기능.
- 모듈 및 앱 프로필 백업 및 복원 지원.
- [앱 프로필](https://kernelsu.org/guide/app-profile.html): 루트 권한을 케이지 안에 가둡니다.

---

## ✅ 호환성

KernelSU Next는 **4.4부터 6.18까지**의 Android 커널을 지원합니다.

| 커널 버전 | 지원 노트 |
|----------------------|-------------------------------------------------------------------------|
| 5.10+ (GKI 2.0)      | 사전 빌드 이미지 및 LKM/KMI 지원 |
| 4.19 – 5.4 (GKI 1.0) | KernelSU 드라이버가 커널에 내장되어 있어야 함 |
| < 4.14 (EOL)         | KernelSU 드라이버가 필요함 (3.18+는 실험적이며 백포트가 필요할 수 있음) |

**지원 아키텍처:** `arm64-v8a` 및 `x86_64`

> [!CAUTION]
> 최근 커널 버전은 KernelSU Next가 실패하고 `x86_64`에서 커널 패닉을 유발할 수 있는 호환성 문제를 도입했습니다! 자세한 내용은 웹사이트를 확인하세요!

---

## 📦 설치

설치 절차는 [설치 가이드](https://kernelsu-next.github.io/webpage/pages/installation.html)를 참조하세요.

---

## 🏅 기여

- 관리자의 번역을 제출하려면 [Crowdin](https://crowdin.com/project/kernelsu-next)으로 이동하세요!
- 보안 이슈를 보고하려면 [SECURITY.md](/SECURITY.md)를 확인하세요.

---

## 📜 라이선스

- **`/kernel` 디렉터리:** [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html).
- **기타 모든 파일:** [GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html).

---

## 💸 후원

프로젝트를 지원하고 싶다면:

- **USDT (BEP20, ERC20)**: `0x0b269716dc00692676e4217e23c7f67abd5f0f3e`
- **USDT (TRC20)**: `TXu6VX8dvJLQPx4WBobjbYT5tefHTDikWo`
- **LTC**: `LMYRqRXPKAFpYncBhNS44W1iwFit14LH1u`

---

## 🙏 크레딧

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/) – 개념 영감
- [Magisk](https://github.com/topjohnwu/Magisk) – 핵심 루트 구현
- [Genuine](https://github.com/brevent/genuine/) – APK v2 서명 검증
- [Diamorphine](https://github.com/m0nad/Diamorphine) – Rootkit 기법
- [KernelSU](https://github.com/tiann/KernelSU) – KernelSU Next를 가능하게 한 원본 기반
- [Susfs4ksu](https://gitlab.com/simonpunk/susfs4ksu) – KernelSU용 커널 수준 루트 은폐 확장
- [Susfs4ksu-lkm](https://github.com/inforcqb/susfs4ksu-lkm) – 로드 가능한 커널 모듈로 포팅된 SUSFS

