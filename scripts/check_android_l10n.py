#!/usr/bin/env python3
"""Fail CI if the Arabic Android manager resources drift from the canonical set.

Stdlib only: python scripts/check_android_l10n.py
"""
from __future__ import annotations

from collections import Counter
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
RES = ROOT / "manager/app/src/main/res"
SRC = ROOT / "manager/app/src/main/java"
SOURCE = RES / "values/strings.xml"
TARGET = RES / "values-ar/strings.xml"
CONFIG = RES / "xml/locales_config.xml"

# Handles standard Android Formatter placeholder syntax, including positional arguments.
FORMAT = re.compile(r"%(?:(?:\d+)\$)?[-+#0 ,.(]*\d*(?:\.\d+)?[a-zA-Z%]")
RTL_TEXT = re.compile(r"[\u0600-\u06ff]")
failures: list[str] = []

def check(condition: bool, reason: str):
    if not condition:
        failures.append(reason)

def read_strings(path: Path) -> dict[str, ET.Element]:
    root = ET.parse(path).getroot()
    check(root.tag == "resources", f"{path}: expected resources root")
    strings = [node for node in root if node.tag == "string"]
    names = [node.attrib["name"] for node in strings]
    duplicates = [key for key, count in Counter(names).items() if count != 1]
    check(not duplicates, f"{path}: duplicate keys: {duplicates}")
    return {node.attrib["name"]: node for node in strings}

try:
    en, ar = read_strings(SOURCE), read_strings(TARGET)
    expected = {
        key for key, value in en.items()
        if value.attrib.get("translatable", "true") != "false"
    }
    check(set(ar) == expected,
          f"Arabic keys differ: missing={sorted(expected-set(ar))}, extra={sorted(set(ar)-expected)}")

    empty = [key for key in expected & ar.keys()
             if not (ar[key].text or "").strip()]
    check(not empty, f"Empty Arabic translations: {empty}")

    mismatches = []
    for key in sorted(expected & ar.keys()):
        left = sorted(FORMAT.findall("".join(en[key].itertext())))
        right = sorted(FORMAT.findall("".join(ar[key].itertext())))
        if left != right:
            mismatches.append((key, left, right))
    check(not mismatches, f"Format placeholders do not match: {mismatches}")

    # The non-Arabic entries below are intentional technical symbols/URLs/brand identifiers.
    technical = {
        "issue_report_github_link", "issue_report_telegram_link",
        "zygisk", "meta_module", "home_susfs_version", "sulog_pid", "sulog_uid",
    }
    untranslated = []
    for key in sorted(expected & ar.keys()):
        text = "".join(ar[key].itertext()).strip()
        if not RTL_TEXT.search(text) and key not in technical:
            untranslated.append((key,text))
    check(not untranslated, f"Untranslated Arabic strings: {untranslated}")

    language_xml = CONFIG.read_text(encoding="utf-8")
    check('<locale android:name="ar" />' in language_xml, "Arabic locale missing from locales_config.xml")
    manifest = (ROOT/"manager/app/src/main/AndroidManifest.xml").read_text(encoding="utf-8")
    check('android:supportsRtl="true"' in manifest, "RTL support disabled in AndroidManifest.xml")

    # Avoid accidental regressions of the runtime UI literals fixed during the migration.
    forbidden = [
        '"Add Repository" else "Edit Repository"',
        'Text("Cancel")',
        'contentDescription = "Manage Repositories"',
        'contentDescription = "Shortcut icon"',
        'Toast.makeText(this, "Invalid data URL"',
        'Toast.makeText(context, "TODO"',
        'showSnackbar("Error downloading module:',
        'showSnackbar("Log saved to ',
    ]
    kt = "\n".join(path.read_text(encoding="utf-8") for path in SRC.rglob("*.kt"))
    check(not [term for term in forbidden if term in kt],
          "Runtime English literals reintroduced")

    # All local resource references must point to an English source key.
    references = set(re.findall(r"(?<!android\.)\bR\.string\.(\w+)", kt))
    check(not (references - set(en)),
          f"Missing default string resources: {sorted(references-set(en))}")

    print(f"Arabic localization: {len(ar)}/{len(expected)} keys, "
          f"{len(references)} Kotlin string-resource references")
except (ET.ParseError, OSError, ValueError) as exc:
    failures.append(str(exc))

if failures:
    print("Arabic localization checks FAILED:", file=sys.stderr)
    for failure in failures:
        print(f" - {failure}", file=sys.stderr)
    sys.exit(1)
print("Arabic localization checks PASSED.")
