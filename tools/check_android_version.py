#!/usr/bin/env python3
"""Reject Android package metadata that would downgrade the target branch's installed APK."""

from __future__ import annotations

import argparse
from pathlib import Path
import re

MAX_VERSION_CODE = 2_100_000_000
CODE_RE = re.compile(r'android:versionCode="([0-9]+)"')
# The release/update contract uses major.minor names. Accept a three-component baseline as well,
# since older target branches may still carry an unreleased patch-style version.
NAME_RE = re.compile(r'android:versionName="([0-9]+(?:\.[0-9]+){1,2})"')


class ManifestVersionError(ValueError):
    """A manifest does not have one valid Android version pair."""


def parse_manifest(text: str) -> tuple[str, int, tuple[int, int, int]]:
    code_matches = list(CODE_RE.finditer(text))
    name_matches = list(NAME_RE.finditer(text))
    if len(code_matches) != 1 or len(name_matches) != 1:
        raise ManifestVersionError("expected exactly one numeric android:versionCode and supported android:versionName")

    code = int(code_matches[0].group(1))
    if not 1 <= code <= MAX_VERSION_CODE:
        raise ManifestVersionError(f"android:versionCode must be between 1 and {MAX_VERSION_CODE}")

    name = name_matches[0].group(1)
    components = tuple(int(part) for part in name.split("."))
    order = components + (0,) * (3 - len(components))
    return name, code, order


def ensure_versions_not_downgrade(
    candidate: tuple[str, int, tuple[int, int, int]],
    baseline: tuple[str, int, tuple[int, int, int]],
) -> None:
    candidate_name, candidate_code, candidate_order = candidate
    baseline_name, baseline_code, baseline_order = baseline

    if candidate_code < baseline_code:
        raise ManifestVersionError(
            f"candidate versionCode {candidate_code} is below target baseline {baseline_code}; "
            "Android will reject an in-place install"
        )
    if candidate_order < baseline_order:
        raise ManifestVersionError(
            f"candidate versionName {candidate_name} is below target baseline {baseline_name}"
        )
    if candidate_order > baseline_order and candidate_code == baseline_code:
        raise ManifestVersionError(
            f"candidate versionName {candidate_name} advances without a higher versionCode; "
            "the updater cannot offer it as an upgrade"
        )


def ensure_not_downgrade(candidate_text: str, baseline_text: str) -> None:
    ensure_versions_not_downgrade(parse_manifest(candidate_text), parse_manifest(baseline_text))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("candidate", type=Path, help="manifest being built")
    parser.add_argument("baseline", type=Path, help="target-branch manifest to compare against")
    args = parser.parse_args()
    try:
        candidate = parse_manifest(args.candidate.read_text(encoding="utf-8"))
        baseline = parse_manifest(args.baseline.read_text(encoding="utf-8"))
        ensure_versions_not_downgrade(candidate, baseline)
    except (OSError, ManifestVersionError) as error:
        parser.exit(1, f"Android version check: {error}\n")
    print(
        f"Android package version {candidate[0]} (versionCode {candidate[1]}) is not below "
        f"target baseline {baseline[0]} (versionCode {baseline[1]})"
    )


if __name__ == "__main__":
    main()
