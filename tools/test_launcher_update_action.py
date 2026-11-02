#!/usr/bin/env python3
"""Source policy for exact updates, persistent official archives, and download recovery."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "android/java/com/protodroid/UpdateManager.java").read_text(encoding="utf-8")
view = (ROOT / "android/launcher/src/commonMain/kotlin/com/protodroid/launcher/LauncherApp.kt").read_text(encoding="utf-8")

primary = source.split("void onUpdatePressed()", 1)[1].split("private void onLatestReleasePressed()", 1)[0]
available = primary.split("case State.AVAILABLE:", 1)[1].split("case State.READY:", 1)[0]
assert "onLatestReleasePressed();" in available
assert "beginDownload();" not in available, "available-update action must open the release, not start a download"
latest = source.split("private void onLatestReleasePressed()", 1)[1].split("void onReleasesPressed()", 1)[0]
assert "release.htmlUrl" in latest and "LATEST_RELEASE_URL" in latest
archive = source.split("void onReleasesPressed()", 1)[1].split("private void openRelease", 1)[0]
assert "openRelease(RELEASES_URL);" in archive and "release.htmlUrl" not in archive
assert 'RELEASES_URL = "https://github.com/Nazatric/ProtoDroid/releases";' in source
assert "Intent.ACTION_VIEW" in source and "private void openRelease" in source
assert "private void verifyDownload" in source and "private void launchInstaller" in source, (
    "existing system downloads must retain verification and install recovery"
)

# Compose About exposes the archive even when no update/error card is visible, plus the signature-change warning.
assert 'OutlineButton("ALL RELEASES", host::openReleases)' in view
assert 'OutlineButton("CHECK UPDATES", host::checkUpdates)' in view
assert "private fun UpdateWarning" in view and "host::confirmUpdate" in view
assert "UNINSTALL & INSTALL" in view and "Shared game files and saves in /Prototype stay in place" in view
print("Exact-release updates, persistent official archives, and download recovery checks passed")
