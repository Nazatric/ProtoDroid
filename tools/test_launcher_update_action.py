#!/usr/bin/env python3
"""Source policy for exact updates, persistent official archives, and download recovery."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "android/java/com/protodroid/UpdateManager.java").read_text(encoding="utf-8")
view = (ROOT / "android/java/com/protodroid/SetupView.java").read_text(encoding="utf-8")

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

# Landscape and portrait expose the archive even when no update/error card is visible.
assert view.count('ACTION_UPDATE_RELEASES, archiveBounds, "VERSIONS"') == 2
assert "if (archiveBounds.contains(x, y)) return ACTION_UPDATE_RELEASES;" in view
assert "settingsOrder.add(ACTION_UPDATE_RELEASES);" not in view, "do not crowd the existing Settings footer"
print("Exact-release updates, persistent official archives, and download recovery checks passed")
