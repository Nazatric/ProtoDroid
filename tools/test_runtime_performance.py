#!/usr/bin/env python3
"""Source-level guards for the bounded ordinary-play overhead reductions and Auto recovery policy."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
kernel = (ROOT / "src/native/kernel32.cpp").read_text(encoding="utf-8")
for name in ("ReadFile", "WriteFile"):
    body = kernel.split(f"K({name})", 1)[1].split("\nK(", 1)[0]
    assert "const bool timing = DetailedDiagnosticsEnabled();" in body
    assert "if (timing) {" in body
    assert body.index("const bool timing") < body.index("ssize_t r =")
    # The syscall remains unconditional; only its clocks/counters are conditional.
    syscall = "pread" if name == "ReadFile" else "pwrite"
    assert f"ssize_t r = ov ? {syscall}" in body
print("Disabled-diagnostics file-I/O timing policy checks passed")

perf = (ROOT / "src/native/perf_hint.cpp").read_text(encoding="utf-8")
frame = perf.split("void PerfHintFrame(int64_t work_ns)", 1)[1].split("PerfHintSnapshot GetPerfHintStats", 1)[0]
assert frame.index("g_session_active.load") < frame.index("std::lock_guard<std::mutex> lock")
assert "g_session_active.store(g_session != nullptr" in perf
print("No-session ADPF fast-path checks passed")

present = (ROOT / "src/native/present_stats.cpp").read_text(encoding="utf-8")
record = present.split("int64_t RecordPresent()", 1)[1].split("uint64_t PresentedFrameCount()", 1)[0]
ordinary = record.split("LightWindow& window", 1)[1]
assert "g_present_mode.load" in record and "if (mode & 1u)" in record
assert "g_stats_mutex" not in ordinary, "ordinary Present bookkeeping must not take the detailed stats lock"
assert "window.us[window.next]" in ordinary and "FinishLightWindow(window, now)" in ordinary
assert "window.generation != generation" in ordinary, "diagnostic mode transitions must rebase ordinary stats"
print("Ordinary Present lock-free/bounded-window checks passed")

main = (ROOT / "src/native/main.cpp").read_text(encoding="utf-8")
config_policy = (ROOT / "src/native/config_policy.h").read_text(encoding="utf-8")
settings = (ROOT / "android/java/com/protodroid/GameSettings.java").read_text(encoding="utf-8")
touch_controls = (ROOT / "android/java/com/protodroid/TouchControls.java").read_text(encoding="utf-8")
assert "AndroidFramePacingDxvkConfig(dxvk_config)" in main
assert "inline constexpr int kDefaultFpsLimit = 30;" in config_policy
assert 'SettingInt("fps_limit", kDefaultFpsLimit)' in main
assert "g_fps_limit{kDefaultFpsLimit}" in main
assert "ApplyBatterySaverFpsLimit(fps_limit, SettingInt(\"battery_saver\", 0) != 0)" in main
assert "PerfHintStart(fps_limit)" in main and "PerfHintTarget(fps)" in main
assert 'Log("frame pacing: engine FPS cap %d%s"' in main
assert "static final int FPS_LIMIT_DEFAULT = 30;" in settings
fps_setting = settings.split('new Setting("FPS limit"', 1)[1].split("new Setting(", 1)[0]
assert 'new String[] {"Off", "30", "45", "60", "90", "120", "144", "240", "500", "1000"}' in fps_setting
assert 'new int[] {0, FPS_LIMIT_DEFAULT, 45, 60, 90, 120, 144, 240, 500, 1000}' in fps_setting
fps_defaults = settings.split("int defaultIndex()", 1)[1].split("static int effectiveFpsLimit", 1)[0]
assert 'key.equals("fps_limit")' in fps_defaults and "values[i] == FPS_LIMIT_DEFAULT" in fps_defaults
assert "GameSettings.effectiveFpsLimit(requested, batterySaver)" in touch_controls
print("30 FPS engine default, explicit overrides, ADPF target, and one-frame D3D9 queue checks passed")

driver = (ROOT / "src/native/vulkan_driver.cpp").read_text(encoding="utf-8")
failed = driver.split("else if (std::string why = FailedBefore()", 1)[1].split("const std::string model", 1)[0]
assert "AutomaticSystemDevice();" in failed, "failed Auto Turnip must enter the fresh direct/wrapper policy"
assert "trying the driver again" not in failed, "same-build failed Turnip retry returned"
assert 'mode == "turnip" || mode == "custom"' in driver, "explicit driver retry policy changed"
print("Auto failed-driver direct/wrapper recovery checks passed")
