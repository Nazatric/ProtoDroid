#!/usr/bin/env python3
"""Startup compatibility policy for the launcher's optional immersive mode."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "android/java/com/protodroid/ImmersiveMode.java").read_text(encoding="utf-8")
outer, modern = source.split("private static final class Api30Impl", 1)

assert "Build.VERSION.SDK_INT >= 30" in outer
assert "WindowInsetsController" not in outer, "API-30 controller types must not leak into launcher startup verification"
assert "WindowInsets.Type" not in outer, "API-30 insets types must stay inside the guarded implementation"
assert "catch (RuntimeException | LinkageError" in outer, "cosmetic immersive failures must not close the launcher"
assert "Api30Impl.apply(window);" in outer
assert "applyLegacy(window);" in outer
assert "WindowInsetsController" in modern and "WindowInsets.Type.statusBars()" in modern
assert "WindowInsets.Type.navigationBars()" in modern
assert "BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE" in modern
assert "controller == null" in modern and "applyLegacy(window);" in modern

activity = (ROOT / "android/launcher/src/androidMain/kotlin/com/protodroid/SetupActivity.kt").read_text(encoding="utf-8")
assert "ImmersiveMode.apply(this)" in activity
print("Immersive startup API isolation and non-fatal fallback checks passed")
