# Arabic localization — KernelSU-Next Manager

Arabic (ar) is a first-class manager locale. It uses the existing application language selector, Android 13+ LocaleManager, pre-13 locale persistence, and android:supportsRtl. The kernel and root permission model are unchanged.

## Resource files

English source: manager/app/src/main/res/values/strings.xml
Arabic target: manager/app/src/main/res/values-ar/strings.xml
Locale declaration: manager/app/src/main/res/xml/locales_config.xml
Language switching: manager/app/src/main/java/com/rifsxd/ksunext/ui/util/LocaleHelper.kt
Validator: scripts/check_android_l10n.py

The canonical key set is the English resource. Keep all translatable keys in Arabic; preserve printf placeholders exactly, including positional indices and types. Escape Android string apostrophes and XML metacharacters. Do not translate URLs, API identifiers, paths, package names, commands, original log contents, SELinux labels, names of modules, or remote metadata.

## Terminology

| English | Arabic |
| --- | --- |
| Home | الرئيسية |
| Superuser | صلاحيات الروت |
| Module | وحدة |
| Grant root | منح صلاحيات الروت |
| Enable | تفعيل |
| Disable | تعطيل |
| Flash | تفليش |
| Restore | استعادة |
| Kernel | النواة |
| Repository | مستودع |
| Profile | ملف شخصي |
| Reboot | إعادة التشغيل |
| Changelog | سجل التغييرات |

Use clear Modern Standard Arabic and short unambiguous labels. Preserve original security warning semantics especially for boot, uninstall, root authorization, ADB, and SELinux.

## RTL and mixed-script content

The Android framework mirrors most layouts. The manager navigation swipe and drag selection are adjusted to mirror with RTL visual order. Keep terminal output, paths, identifiers and version values unchanged; apply BidiFormatter to mix paths with Arabic UI messages when applicable. Externally authored third-party module WebUIs are outside manager localization scope, and must not be translated forcibly.

## Repeatable checks

    python scripts/check_android_l10n.py
    cd manager
    ./gradlew :app:assembleDebug
    ./gradlew :app:lintDebug

CI checks: XML validity, exact coverage, duplicate keys, matching format placeholders, non-Arabic string exceptions, app-default resource references, Android RTL support, and a regression guard against known direct English UI strings. Existing manager CI handles regular Android build validation.

## Manual validation matrix (required before merge)

- Arabic, English, System default, including locale persistence after restart; test before Android 13 and Android 13+.
- Home, Superuser, modules, module repositories, Settings, Customization, application profile, templates, backup/restore, installation, logs, lock screen and embedded WebUI host.
- RTL bottom navigation selection, horizontal page swipes and pill dragging, with LTR regression tests.
- Light/dark mode, system font scale 1.0, 1.3 and 1.8, small screens, TalkBack and landscape where supported.
- Mixed-language links and paths, UID/PID, SELinux contexts, version strings, placeholders and descriptions.
- Safety: verify destructive-action warnings and dialogs; run visual tests without performing flashing or granting actual sensitive privileges.

Capture screenshots and test conditions. Device-only tests cannot be marked passed until run on a real device/emulator.

## Maintenance and upstream sync

For every upstream code update, run localization checks and translate any new source keys before release. Review any new raw Kotlin UI text; keep all externally visible manager-owned strings in Android resources. Only merge when checks, actual builds and manual UI/RTL verification are successful.
