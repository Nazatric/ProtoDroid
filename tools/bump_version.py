#!/usr/bin/env python3
"""Inspect or increment ProtoDroid's Android package version.

The manifest is the single source of truth. A bump advances the visible major.minor
version and the integer versionCode together, so a published build cannot silently
keep Android's old update identity.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import tempfile

CODE_RE = re.compile(r'android:versionCode="([0-9]+)"')
NAME_RE = re.compile(r'android:versionName="([0-9]+)\.([0-9]+)"')
MAX_VERSION_CODE = 2_100_000_000


class VersionError(ValueError):
    """The manifest does not contain exactly one supported version pair."""


def read_version(text: str) -> tuple[str, int]:
    code_matches = list(CODE_RE.finditer(text))
    name_matches = list(NAME_RE.finditer(text))
    if len(code_matches) != 1 or len(name_matches) != 1:
        raise VersionError("expected exactly one numeric android:versionCode and major.minor android:versionName")
    code = int(code_matches[0].group(1))
    if not 1 <= code <= MAX_VERSION_CODE:
        raise VersionError(f"android:versionCode must be between 1 and {MAX_VERSION_CODE}")
    name_match = name_matches[0]
    return f"{int(name_match.group(1))}.{int(name_match.group(2))}", code


def bumped_text(text: str) -> tuple[str, str, int, str, int]:
    old_name, old_code = read_version(text)
    if old_code == MAX_VERSION_CODE:
        raise VersionError("android:versionCode cannot be incremented further")
    major, minor = (int(part) for part in old_name.split("."))
    new_name = f"{major}.{minor + 1}"
    new_code = old_code + 1
    new_text, code_count = CODE_RE.subn(f'android:versionCode="{new_code}"', text, count=1)
    new_text, name_count = NAME_RE.subn(f'android:versionName="{new_name}"', new_text, count=1)
    if code_count != 1 or name_count != 1:
        raise VersionError("version fields changed unexpectedly")
    # Catch formatting or replacement mistakes before touching the manifest.
    if read_version(new_text) != (new_name, new_code):
        raise VersionError("incremented manifest failed verification")
    return new_text, old_name, old_code, new_name, new_code


def bump_manifest(path: Path) -> tuple[str, int, str, int]:
    text = path.read_text(encoding="utf-8")
    new_text, old_name, old_code, new_name, new_code = bumped_text(text)
    mode = path.stat().st_mode
    temporary_name = None
    try:
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=path.parent, delete=False) as temporary:
            temporary_name = temporary.name
            temporary.write(new_text)
            temporary.flush()
            os.fsync(temporary.fileno())
        os.chmod(temporary_name, mode)
        os.replace(temporary_name, path)
    finally:
        if temporary_name and os.path.exists(temporary_name):
            os.unlink(temporary_name)
    return old_name, old_code, new_name, new_code


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", nargs="?", type=Path, default=root / "android/AndroidManifest.xml")
    parser.add_argument("--show", action="store_true", help="print 'versionName versionCode' without changing files")
    args = parser.parse_args()
    if args.show:
        name, code = read_version(args.manifest.read_text(encoding="utf-8"))
        print(f"{name} {code}")
        return
    old_name, old_code, new_name, new_code = bump_manifest(args.manifest)
    print(f"ProtoDroid version: {old_name} ({old_code}) -> {new_name} ({new_code})")


if __name__ == "__main__":
    main()
