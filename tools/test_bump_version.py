#!/usr/bin/env python3

import tempfile
from pathlib import Path
import unittest

from bump_version import VersionError, bump_manifest, bumped_text, read_version


MANIFEST = """<?xml version=\"1.0\" encoding=\"utf-8\"?>
<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"
    package=\"com.protodroid\"
    android:versionCode=\"1\"
    android:versionName=\"0.1\">
    <application android:label=\"Prototype\" />
</manifest>
"""


class VersionTests(unittest.TestCase):
    def test_first_and_subsequent_release_increment_both_fields(self):
        once, old_name, old_code, new_name, new_code = bumped_text(MANIFEST)
        self.assertEqual((old_name, old_code, new_name, new_code), ("0.1", 1, "0.2", 2))
        self.assertEqual(read_version(once), ("0.2", 2))
        twice, *result = bumped_text(once)
        self.assertEqual(result, ["0.2", 2, "0.3", 3])
        self.assertEqual(read_version(twice), ("0.3", 3))
        self.assertIn('package="com.protodroid"', twice)
        self.assertIn('android:label="Prototype"', twice)

    def test_minor_versions_are_numeric_not_decimal_strings(self):
        text = MANIFEST.replace('versionCode="1"', 'versionCode="10"').replace('versionName="0.1"', 'versionName="0.9"')
        bumped, *_ = bumped_text(text)
        self.assertEqual(read_version(bumped), ("0.10", 11))

    def test_show_parser_does_not_confuse_other_attributes(self):
        text = MANIFEST.replace('android:versionName="0.1"', 'android:versionName="12.34"')
        self.assertEqual(read_version(text), ("12.34", 1))

    def test_missing_duplicate_and_non_numeric_versions_are_rejected(self):
        invalid = [
            MANIFEST.replace('    android:versionCode="1"\n', ""),
            MANIFEST.replace('android:versionName="0.1"', ""),
            MANIFEST.replace('android:versionCode="1"', 'android:versionCode="one"'),
            MANIFEST.replace('android:versionName="0.1"', 'android:versionName="0.1.1"'),
            MANIFEST.replace('android:versionCode="1"', 'android:versionCode="0"'),
            MANIFEST.replace("</manifest>", '    android:versionCode="2"\n</manifest>'),
        ]
        for text in invalid:
            with self.subTest(text=text), self.assertRaises(VersionError):
                bumped_text(text)

    def test_manifest_update_is_verified_on_disk(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "AndroidManifest.xml"
            path.write_text(MANIFEST, encoding="utf-8")
            self.assertEqual(bump_manifest(path), ("0.1", 1, "0.2", 2))
            self.assertEqual(read_version(path.read_text(encoding="utf-8")), ("0.2", 2))


if __name__ == "__main__":
    unittest.main()
