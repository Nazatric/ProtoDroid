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

d3d9 = (ROOT / "src/native/d3d9.cpp").read_text(encoding="utf-8")
register = d3d9.split("void RegisterPresentPerfHintThread()", 1)[1].split("template <typename F>", 1)[0]
assert "thread_local bool registered = false;" in register
assert register.index("if (registered) return;") < register.index("PerfHintAddThread(int(gettid()))")
guarded = d3d9.split("HRESULT GuardedPresent(F present)", 1)[1].split("#ifdef __ANDROID__", 1)[0]
assert guarded.index("RecordPresent();") < guarded.index("RegisterPresentPerfHintThread();")
assert guarded.index("RegisterPresentPerfHintThread();") < guarded.index("} else {")
assert "thread_updates" in perf and "thread_update_errors" in perf
assert "rejected by dynamic thread update" in perf and "added to active session" in perf
snapshot = (ROOT / "src/native/guest.h").read_text(encoding="utf-8")
assert "uint64_t thread_updates = 0;" in snapshot and "uint64_t thread_update_errors = 0;" in snapshot
print("Actual-Present-thread ADPF registration and update-evidence checks passed")

start = perf.split("void PerfHintStart(int fps_target)", 1)[1].split("// A thread that works on every frame", 1)[0]
assert start.index("AddDxvkFrameThreads(g_tids);") < start.index("detail::LiveHintThreads")
scan = perf.split("size_t AddDxvkFrameThreads", 1)[1].split("}  // namespace", 1)[0]
assert 'detail::IsDxvkFrameThreadComm(comm)' in scan
assert 'opendir("/proc/self/task")' in scan
selector = (ROOT / "src/native/perf_hint_session.h").read_text(encoding="utf-8")
assert 'comm == "dxvk-cs" || comm == "dxvk-submit"' in selector
assert "dxvk-worker" not in selector and "mali-compiler" not in selector
print("DXVK native frame-thread discovery and pruning-order checks passed")

runtime = (ROOT / "src/recomp/runtime.h").read_text(encoding="utf-8")
assert runtime.count("if (RECOMP_ALIGNED(a,") == 6
assert "RECOMP_ALIGNED_LIKELY" not in runtime, "the immaterial ARM64 layout experiment returned"
for width in (16, 32, 64):
    assert f"RecompR{width}Unaligned" not in runtime and f"RecompW{width}Unaligned" not in runtime, (
        "cold helper experiment unexpectedly returned to the inline runtime"
    )
print("Ordered TSO aligned/unaligned fallback preservation checks passed")

main = (ROOT / "src/native/main.cpp").read_text(encoding="utf-8")
assert 'SettingInt("d3d9_timing", detailed_diagnostics ? 1 : 0)' in main
assert 'setenv("PROTODROID_D3D9_TIMING", d3d9_timing ? "1" : "0"' in main
print("Detailed-only D3D9 method-timing default checks passed")

driver = (ROOT / "src/native/vulkan_driver.cpp").read_text(encoding="utf-8")
failed = driver.split("else if (std::string why = FailedBefore()", 1)[1].split("const std::string model", 1)[0]
assert "AutomaticSystemDevice();" in failed, "failed Auto Turnip must enter the fresh direct/wrapper policy"
assert "trying the driver again" not in failed, "same-build failed Turnip retry returned"
assert 'mode == "turnip" || mode == "custom"' in driver, "explicit driver retry policy changed"
print("Auto failed-driver direct/wrapper recovery checks passed")
