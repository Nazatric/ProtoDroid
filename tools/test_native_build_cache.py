#!/usr/bin/env python3
"""Regression checks for safe Android native compiler caching and exact source-revision logging."""

import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAKEFILE = (ROOT / "src/native/Makefile").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/build-apk.yml").read_text(encoding="utf-8")

# Dry-run the production Android rules. No NDK or ccache installation is needed to inspect the commands.
result = subprocess.run(
    [
        "make",
        "-n",
        "-C",
        "src/native",
        "PLATFORM=android",
        "ABI=arm64-v8a",
        "NDK=/tmp/protodroid-test-ndk",
        "OUT=/tmp/protodroid-cache-test",
        "RECOMP_DIR=/tmp/protodroid-cache-test/no-generated-sources",
        "DXVK=/tmp/protodroid-test-dxvk",
        "DEPS=/tmp/protodroid-test-deps",
        "SOURCE_REVISION=cache-test-revision",
        "CCACHE=ccache",
    ],
    cwd=ROOT,
    text=True,
    capture_output=True,
    check=True,
)
commands = [line.strip() for line in result.stdout.splitlines() if " -c -o " in line]
assert commands, "Android make dry-run produced no compile commands"
for command in commands:
    source = command.rsplit(" ", 1)[-1]
    assert command.startswith("ccache /tmp/protodroid-test-ndk/"), (
        f"Android compile bypasses ccache: {command}"
    )
    revision_is_local = "-DPROTODROID_SOURCE_REVISION" in command
    revision_is_used = source in {"main.cpp", "diagnostic_tour.cpp"}
    assert revision_is_local == revision_is_used, (
        f"source revision must invalidate only its logging translation units: {command}"
    )
assert {line.rsplit(" ", 1)[-1] for line in commands if "-DPROTODROID_SOURCE_REVISION" in line} == {
    "main.cpp",
    "diagnostic_tour.cpp",
}
assert "CCACHE ?=" in MAKEFILE

assert "apt-get install -y --no-install-recommends ninja-build glslang-tools pkg-config python3-pip p7zip-full unzip ccache" in WORKFLOW
assert "CCACHE_DIR: ${{ github.workspace }}/build/ccache" in WORKFLOW
assert "CCACHE_BASEDIR: ${{ github.workspace }}" in WORKFLOW
assert "CCACHE_COMPILERCHECK: content" in WORKFLOW
assert "CCACHE_MAXSIZE: 2G" in WORKFLOW
assert "path: build/ccache" in WORKFLOW
assert "key: protodroid-android-ccache-v1-${{ runner.os }}-${{ github.sha }}" in WORKFLOW
assert "protodroid-android-ccache-v1-${{ runner.os }}-" in WORKFLOW
assert WORKFLOW.count("CCACHE=ccache -j1") == 2, "both Android ISA variants must use the same cache"
assert "run: ccache --show-stats" in WORKFLOW

print("Android native ccache wiring and source-revision cache scope checks passed")
