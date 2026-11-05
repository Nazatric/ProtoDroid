#!/usr/bin/env python3
"""Guard the DXVK-only ThinLTO build policy and its effective-command verification."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = (ROOT / "android/build-deps.sh").read_text(encoding="utf-8")


def validate(text: str) -> None:
    setup = text.split('meson setup "$SRC/build/dxvk-$ABI"', 1)[1].split(
        '\nninja -C "$SRC/build/dxvk-$ABI" -j "$JOBS"', 1
    )[0]
    assert "-Db_lto=true" in setup, "DXVK LTO is not enabled"
    assert "-Db_lto_mode=thin" in setup, "DXVK must use ThinLTO"
    assert "-Ddxvk_native_wsi=sdl2" in setup, "the Android SDL2 WSI selection changed"
    assert "ninja -C \"$SRC/build/dxvk-$ABI\" -t commands | grep -F -- '-flto=thin'" in setup, (
        "the effective NDK compile/link commands are not verified"
    )
    # The optimized dependency must remain usable by the explicit ARMv8.0 native compatibility variant.
    assert "CPU_FAMILY=aarch64; CPU=armv8-a" in text, "the generic ARM64 dependency target changed"
    assert text.count("meson setup \"$SRC/build/dxvk-$ABI\"") == 1


validate(BUILD)
for name, mutated in {
    "LTO disabled": BUILD.replace("-Db_lto=true", "-Db_lto=false", 1),
    "wrong LTO mode": BUILD.replace("-Db_lto_mode=thin", "-Db_lto_mode=full", 1),
    "effective-command check removed": BUILD.replace(
        "ninja -C \"$SRC/build/dxvk-$ABI\" -t commands | grep -F -- '-flto=thin'", "true", 1
    ),
    "compatibility target narrowed": BUILD.replace("CPU_FAMILY=aarch64; CPU=armv8-a", "CPU_FAMILY=aarch64; CPU=armv8.2-a", 1),
}.items():
    try:
        validate(mutated)
    except (AssertionError, IndexError):
        pass
    else:
        raise AssertionError(f"mutation was not detected: {name}")

print("DXVK ThinLTO build policy checks passed; four mutations detected")
