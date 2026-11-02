#!/usr/bin/env python3
"""Unit and production-wiring tests for SDL Java/native version provenance."""
from pathlib import Path
import tempfile

from sdl_version import pinned_version, source_version, verify_class, verify_manifest, write_manifest

ROOT = Path(__file__).resolve().parents[1]


def expect_failure(action, description):
    try:
        action()
    except (ValueError, OSError):
        return
    raise AssertionError(description)


with tempfile.TemporaryDirectory(prefix="protodroid-sdl-version-test-") as directory:
    root = Path(directory)
    activity = root / "android-project/app/src/main/java/org/libsdl/app/SDLActivity.java"
    header = root / "include/SDL_version.h"
    activity.parent.mkdir(parents=True)
    header.parent.mkdir(parents=True)
    header.write_text(
        "#define SDL_MAJOR_VERSION 2\n#define SDL_MINOR_VERSION 30\n#define SDL_PATCHLEVEL 9\n",
        encoding="utf-8",
    )
    activity.write_text(
        "private static final int SDL_MAJOR_VERSION = 2;\n"
        "private static final int SDL_MINOR_VERSION = 30;\n"
        "private static final int SDL_MICRO_VERSION = 9;\n",
        encoding="utf-8",
    )
    assert source_version(root) == "2.30.9"

    library = root / "libSDL2.so"
    manifest = root / "protodroid-sdl2-version.txt"
    library.write_bytes(b"native SDL fixture")
    write_manifest(root, library, manifest, "2.30.9")
    verify_manifest(manifest, library, "2.30.9")
    expect_failure(lambda: verify_manifest(manifest, library, "2.32.10"), "native version mismatch was accepted")
    library.write_bytes(b"substituted native library")
    expect_failure(lambda: verify_manifest(manifest, library, "2.30.9"), "native hash mismatch was accepted")

    activity.write_text(activity.read_text(encoding="utf-8").replace("SDL_MICRO_VERSION = 9", "SDL_MICRO_VERSION = 10"), encoding="utf-8")
    expect_failure(lambda: source_version(root), "C/Java source mismatch was accepted")

    javap = root / "SDLActivity.javap"
    javap.write_text(
        "private static final int SDL_MAJOR_VERSION;\n  ConstantValue: int 2\n"
        "private static final int SDL_MINOR_VERSION;\n  ConstantValue: int 30\n"
        "private static final int SDL_MICRO_VERSION;\n  ConstantValue: int 9\n",
        encoding="utf-8",
    )
    verify_class(javap, "2.30.9")
    expect_failure(lambda: verify_class(javap, "2.32.10"), "compiled Java version mismatch was accepted")

assert pinned_version(ROOT / "android/build-deps.sh") == "2.30.9"
deps = (ROOT / "android/build-deps.sh").read_text(encoding="utf-8")
packager = (ROOT / "android/build-apk.sh").read_text(encoding="utf-8")
workflow = (ROOT / ".github/workflows/build-apk.yml").read_text(encoding="utf-8")
assert "write-manifest" in deps and "protodroid-sdl2-version.txt" in deps
assert "verify-manifest" in packager and "verify-class" in packager
assert "Verified original native launcher and signed SDL" in packager
assert 'apk.read("classes.dex") != Path(dex_path).read_bytes()' in packager
assert 'apk.read(name) != staged.read_bytes()' in packager
assert "release-2.30.9" in workflow and "release-2.32.10" not in workflow
assert "deps-out/${{ matrix.abi }}/lib/protodroid-sdl2-version.txt" in workflow
assert "test -s \"$HOME/protodroid-data/android-deps/arm64-v8a/lib/protodroid-sdl2-version.txt\"" in workflow
assert "javap -classpath /tmp/protodroid-java-classes" in workflow

print("SDL source, compiled-Java, native-provenance, and signed-package mismatch checks passed")
