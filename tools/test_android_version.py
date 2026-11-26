#!/usr/bin/env python3
"""Host tests for the Android update-version floor used by pull-request CI."""

import re
import unittest
from pathlib import Path

from check_android_version import ManifestVersionError, ensure_not_downgrade, parse_manifest


def manifest(version_code: int, version_name: str) -> str:
    return f'''<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    android:versionCode="{version_code}"
    android:versionName="{version_name}" />
'''


class AndroidVersionFloorTests(unittest.TestCase):
    def test_forward_major_minor_release_can_follow_patch_style_base(self):
        ensure_not_downgrade(manifest(26, "0.18"), manifest(25, "0.17.6"))

    def test_current_manifest_matches_the_top_changelog_section(self):
        root = Path(__file__).resolve().parents[1]
        current_name, _, _ = parse_manifest((root / "android/AndroidManifest.xml").read_text(encoding="utf-8"))
        headings = re.findall(
            r"^## ([0-9]+(?:\.[0-9]+){1,2})(?:\s+-\s+[0-9]{4}-[0-9]{2}-[0-9]{2})?\s*$",
            (root / "CHANGELOG.md").read_text(encoding="utf-8"),
            flags=re.MULTILINE,
        )
        self.assertTrue(headings, "changelog has no release section")
        self.assertEqual(headings[0], current_name)

    def test_unchanged_target_version_is_allowed(self):
        ensure_not_downgrade(manifest(25, "0.17.6"), manifest(25, "0.17.6"))

    def test_rejects_android_version_code_rollback(self):
        with self.assertRaisesRegex(ManifestVersionError, "versionCode 16.*baseline 25"):
            ensure_not_downgrade(manifest(16, "0.18"), manifest(25, "0.17.6"))

    def test_rejects_visible_version_rollback_even_with_higher_code(self):
        with self.assertRaisesRegex(ManifestVersionError, "versionName 0.16.*0.17.6"):
            ensure_not_downgrade(manifest(26, "0.16"), manifest(25, "0.17.6"))

    def test_rejects_newer_name_without_a_higher_update_code(self):
        with self.assertRaisesRegex(ManifestVersionError, "without a higher versionCode"):
            ensure_not_downgrade(manifest(25, "0.18"), manifest(25, "0.17.6"))

    def test_numeric_components_not_string_order_determine_version(self):
        self.assertGreater(parse_manifest(manifest(27, "0.20"))[2],
                           parse_manifest(manifest(26, "0.19"))[2])
        self.assertGreater(parse_manifest(manifest(27, "0.17.7"))[2],
                           parse_manifest(manifest(26, "0.17.6"))[2])

    def test_rejects_invalid_or_ambiguous_manifest_metadata(self):
        for text in (
            manifest(0, "0.18"),
            manifest(2_100_000_001, "0.18"),
            manifest(26, "0.18.0.1"),
            manifest(26, "0.18") + manifest(26, "0.18"),
        ):
            with self.subTest(text=text), self.assertRaises(ManifestVersionError):
                parse_manifest(text)


if __name__ == "__main__":
    unittest.main()
