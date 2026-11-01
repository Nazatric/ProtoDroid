#!/usr/bin/env python3
"""Source policy for the requested direct-release update action and retained download recovery."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "android/java/com/protodroid/UpdateManager.java").read_text(encoding="utf-8")
primary = source.split("void onUpdatePressed()", 1)[1].split("void onReleasesPressed()", 1)[0]
available = primary.split("case State.AVAILABLE:", 1)[1].split("case State.READY:", 1)[0]
assert "onReleasesPressed();" in available
assert "beginDownload();" not in available, "available-update Download must open the release, not start a background download"
open_release = source.split("void onReleasesPressed()", 1)[1].split("/** Called only after", 1)[0]
assert "release.htmlUrl" in open_release and "RELEASES_URL" in open_release
assert "Intent.ACTION_VIEW" in open_release
assert "private void verifyDownload" in source and "private void launchInstaller" in source, (
    "existing system downloads must retain verification and install recovery"
)
print("Direct-release update action and existing-download recovery checks passed")
