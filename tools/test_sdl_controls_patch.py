#!/usr/bin/env python3
"""Apply the ordered SDL patches to a fresh pinned clone and verify standard Num Lock reaches SDL."""

from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="protodroid-sdl-controls-") as directory:
    subprocess.run(
        ["git", "clone", "-q", "--depth", "1", "--branch", "release-2.30.9",
         "https://github.com/libsdl-org/SDL.git", directory],
        check=True,
    )
    for patch in sorted((ROOT / "android/patches").glob("sdl2-*.patch")):
        subprocess.run(["git", "-C", directory, "apply", "--check", str(patch)], check=True)
        subprocess.run(["git", "-C", directory, "apply", str(patch)], check=True)
    source = (Path(directory) / "src/video/android/SDL_androidkeyboard.c").read_text(encoding="utf-8")

assert "SDL_SCANCODE_NUMLOCKCLEAR,     /* AKEYCODE_NUM_LOCK */" in source
assert "SDL_SCANCODE_UNKNOWN,          /* AKEYCODE_NUM_LOCK */" not in source
print("Fresh SDL 2.30.9 ordered patches and Num Lock key mapping passed")
