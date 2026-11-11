#!/usr/bin/env python3
"""Validate sequential release versions and explicit signing-change approvals."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
from typing import Any


def verify_release(version_name: str, version_code: int, certificate: str, releases: list[dict[str, Any]],
                   identity: str, allow_ephemeral: bool, allow_certificate_change: bool) -> bool:
    """Return whether the certificate changed, or raise ValueError when publication is unsafe."""
    if not re.fullmatch(r"[0-9]+\.[0-9]+", version_name) or version_code < 1:
        raise ValueError("invalid verified APK version metadata")
    if not re.fullmatch(r"[0-9a-f]{64}", certificate):
        raise ValueError("invalid verified APK certificate digest")
    if identity not in {"persistent", "ephemeral"}:
        raise ValueError("unknown APK signing identity")
    if identity == "ephemeral" and not allow_ephemeral:
        raise ValueError("disposable-signed releases require allow_ephemeral_release")

    current = tuple(map(int, version_name.split(".")))
    versions: list[tuple[int, int]] = []
    codes: list[int] = []
    certificates: dict[tuple[int, int], str] = {}
    for release in releases:
        match = re.fullmatch(r"protodroid-v([0-9]+)\.([0-9]+)", release.get("tag_name", ""))
        body = release.get("body") or ""
        if match:
            version = tuple(map(int, match.groups()))
            versions.append(version)
            previous = re.search(r"Signing certificate SHA-256: `([0-9a-f]{64})`", body)
            if previous:
                certificates[version] = previous.group(1)
        code = re.search(r"versionCode[ =]([0-9]+)", body)
        if code:
            codes.append(int(code.group(1)))
    if versions and current <= max(versions):
        raise ValueError("APK version is not newer than the latest release; run tools/bump_version.py")
    if codes and version_code <= max(codes):
        raise ValueError("APK versionCode is not newer than the latest release; run tools/bump_version.py")

    changed = False
    if versions:
        previous_certificate = certificates.get(max(versions))
        changed = bool(previous_certificate and certificate != previous_certificate)
        if changed and not allow_certificate_change:
            raise ValueError(
                "APK signing certificate differs from the latest release; "
                "an update-breaking release requires allow_certificate_change"
            )
    return changed


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version_name")
    parser.add_argument("version_code", type=int)
    parser.add_argument("certificate")
    parser.add_argument("releases", type=Path)
    parser.add_argument("identity", choices=("persistent", "ephemeral"))
    args = parser.parse_args()
    try:
        releases = json.loads(args.releases.read_text(encoding="utf-8"))
        if not isinstance(releases, list):
            raise ValueError("release API response is not a list")
        changed = verify_release(
            args.version_name,
            args.version_code,
            args.certificate,
            releases,
            args.identity,
            os.environ.get("ALLOW_EPHEMERAL") == "true",
            os.environ.get("ALLOW_CERTIFICATE_CHANGE") == "true",
        )
    except (OSError, ValueError, TypeError, json.JSONDecodeError) as error:
        parser.exit(1, f"release policy: {error}\n")
    print(f"Release version and signing identity accepted: {args.version_name} "
          f"(versionCode {args.version_code}); certificate_changed={str(changed).lower()}")


if __name__ == "__main__":
    main()
