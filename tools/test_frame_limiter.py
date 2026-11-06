#!/usr/bin/env python3
"""Structural and mutation-sensitive guards for successful-Present FPS pacing; not a device FPS benchmark."""
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"unterminated {signature}")


def sources(root: Path) -> dict[str, str]:
    return {
        "main": (root / "src/native/main.cpp").read_text(encoding="utf-8"),
        "d3d9": (root / "src/native/d3d9.cpp").read_text(encoding="utf-8"),
        "limiter": (root / "src/native/frame_limiter.cpp").read_text(encoding="utf-8"),
        "header": (root / "src/native/frame_limiter.h").read_text(encoding="utf-8"),
        "diagnostics": (root / "src/native/diagnostics.cpp").read_text(encoding="utf-8"),
        "diagnostics_h": (root / "src/native/diagnostics.h").read_text(encoding="utf-8"),
        "makefile": (root / "src/native/Makefile").read_text(encoding="utf-8"),
        "dxvk_config": (root / "src/native/dxvk_config.h").read_text(encoding="utf-8"),
        "bink": (root / "src/native/bink.cpp").read_text(encoding="utf-8"),
    }


def verify(root: Path = ROOT) -> int:
    s = sources(root)
    checks = 0

    def check(condition: bool, message: str) -> None:
        nonlocal checks
        checks += 1
        if not condition:
            raise AssertionError(message)

    check("frame_limiter.cpp" in s["makefile"], "production native link omits the limiter")
    check('#include "frame_limiter.h"' in s["main"] and '#include "frame_limiter.h"' in s["d3d9"],
          "production integration include missing")
    check("SetFrameLimitTarget(fps_limit)" in s["main"], "initial target is not published")
    check("SetFrameLimitTarget(fps)" in s["main"] and "PerfHintTarget(fps)" in s["main"],
          "live JNI target or ADPF update was lost")
    check("g_fps_limit" not in s["main"] and "1000000 / limit" not in s["main"] and
          "sleep_until" not in s["main"], "old engine-loop limiter remains")
    check("frame_start" not in function(s["main"], "int main(int argc, char** argv)"),
          "main loop retains post-sleep frame anchoring")
    main_loop = s["main"].split("for (uint64_t frame = 0;; ++frame)", 1)[1].split("if (diagnostic_tour)", 1)[0]
    check("sleep_until" not in main_loop and "LimitPresentedFrame" not in main_loop,
          "engine loop still paces iterations instead of Presents")
    check("RecordEngineLoop(service_end_ns" in main_loop and "limiter_ns" not in main_loop,
          "engine diagnostic still carries a limiter duration")

    guarded = function(s["d3d9"], "HRESULT GuardedPresent(F present)")
    check("if (g_surface_gone)" in guarded and "ResetPresentFrameLimiter();" in guarded,
          "surface-less calls do not reset pacing")
    check(guarded.index("HRESULT hr = present()") < guarded.index("RecordPresent()") <
          guarded.index("lock.unlock()", guarded.index("RecordPresent()")) < guarded.index("LimitPresentedFrame()"),
          "Present/timestamp/unlock/limiter order changed")
    successful = guarded.split("if (SUCCEEDED(hr)) {", 1)[1]
    check("RecordPresent();" in successful, "successful Present no longer records a real interval")
    check("const FrameLimitResult limit = LimitPresentedFrame();" in guarded,
          "dedicated limiter is not called")
    check(guarded.rindex("if (SUCCEEDED(hr)) {") < guarded.index("const FrameLimitResult limit"),
          "failed Present can advance the limiter")
    check(guarded.index("lock.unlock()") < guarded.index("std::this_thread::sleep_for") and
          guarded.index("lock.unlock()") < guarded.index("LimitPresentedFrame()"),
          "a wait can hold the surface mutex")
    check("if (timing) RecordPresentLimiter(limit);" in guarded, "limiter diagnostics are not opt-in")
    reset = function(s["d3d9"], "M(IDirect3DDevice9, Reset)")
    check("ResetPresentFrameLimiter();" in reset, "swapchain reset is not a pacing boundary")

    plan = function(s["limiter"], "FrameLimitPlan FrameLimitSchedule::Plan")
    check("1000000000ll / target_fps_" in s["limiter"] and "1000000000ll % target_fps_" in s["limiter"],
          "nanosecond rational period was replaced")
    advance = function(s["limiter"], "void FrameLimitSchedule::AdvanceDeadline")
    check("fractional_ns_ += period_remainder_" in advance and "++next_deadline_ns_" in advance,
          "fractional deadline residue is discarded")
    check("next_deadline_ns_ = now_ns;" in plan and "plan.schedule_reset = true;" in plan,
          "new target does not establish a fresh baseline")
    check("now_ns - next_deadline_ns_ >= period_ns_" in plan and "plan.late_reset = true;" in plan,
          "full-period late reset is missing")
    check("now_ns + period_ns_" not in plan and "next_deadline_ns_ + period_ns_" not in plan,
          "deadline overflow guard was removed")
    check("std::chrono::microseconds" not in s["limiter"], "whole-microsecond quantization returned")
    check("AdvanceDeadline();" in plan and "NowNs()" not in plan, "scheduler depends on post-sleep reanchoring")

    limited = function(s["limiter"], "FrameLimitResult LimitPresentedFrame()")
    check(limited.index("if (target_fps <= 0)") < limited.index("std::lock_guard<std::mutex>") and
          limited.index("if (target_fps <= 0)") < limited.index("NowNs()"),
          "unlimited fast path acquired a mutex or clock")
    check("sleep_until(deadline)" in limited and "plan.deadline_ns" in limited,
          "production wait does not use the planned absolute deadline")
    after_sleep = limited.split("std::this_thread::sleep_until(deadline);", 1)[1]
    check("g_schedule.Plan" not in after_sleep and "next_deadline_ns_" not in after_sleep,
          "post-sleep schedule reanchoring returned")
    check("result.oversleep_ns" in after_sleep and "wake_ns - plan.deadline_ns" in after_sleep,
          "actual oversleep is not measured")
    setter = function(s["limiter"], "void SetFrameLimitTarget(int target_fps)")
    check("g_target_fps.store" in setter and "g_schedule_dirty.store(true" in setter and
          setter.index("g_schedule_dirty.store(true") < setter.index("g_target_fps.store"),
          "live target changes do not publish invalidation before the target")
    check("g_schedule_dirty.store(true" in function(s["limiter"], "void ResetPresentFrameLimiter()"),
          "external reset does not invalidate the schedule")

    check("kDetailedDiagnosticsDefault = 0" in s["diagnostics_h"], "detailed metrics became a default")
    record = function(s["diagnostics"], "void RecordPresentLimiter(const FrameLimitResult& result)")
    check("result.target_fps <= 0 || !DetailedDiagnosticsEnabled()" in record,
          "disabled/unlimited limiter still records detailed samples")
    check("g_limiter_oversleep.Add" in record and "result.requested_wait_ns > 0" in record,
          "sleep/oversleep attribution was lost")
    check("present limiter calls=" in s["diagnostics"] and "late_resets=" in s["diagnostics"],
          "device limiter evidence is not emitted")
    check("FrameLimitTarget()" in s["diagnostics"] and "g_fps_limit" not in s["diagnostics"],
          "diagnostics reads a stale target")
    check("d3d9.maxFrameRate" not in s["dxvk_config"] and "DXVK_FRAME_RATE" not in s["dxvk_config"],
          "native target was accidentally duplicated into DXVK")
    check("BinkMenuWaitUnlocked(MenuFpsUnlocked()" in s["bink"], "menu/Bink unlock behavior changed")
    return checks


def mutation_checks() -> int:
    mutations = [
        ("src/native/main.cpp", "SetFrameLimitTarget(fps_limit);",
         "SetFrameLimitTarget(fps_limit); std::this_thread::sleep_until(std::chrono::steady_clock::now());"),
        ("src/native/d3d9.cpp", "lock.unlock();\n    if (SUCCEEDED(hr)) {\n        const FrameLimitResult limit = LimitPresentedFrame();",
         "if (SUCCEEDED(hr)) {\n        const FrameLimitResult limit = LimitPresentedFrame();\n        lock.unlock();"),
        ("src/native/frame_limiter.cpp", "fractional_ns_ += period_remainder_;", "fractional_ns_ += 0;"),
        ("src/native/frame_limiter.cpp", "now_ns - next_deadline_ns_ >= period_ns_", "false"),
        ("src/native/frame_limiter.cpp", "g_schedule_dirty.store(true, std::memory_order_release);",
         "g_schedule_dirty.store(false, std::memory_order_release);"),
        ("src/native/diagnostics.cpp", "result.target_fps <= 0 || !DetailedDiagnosticsEnabled()", "false"),
    ]
    detected = 0
    for relative, old, new in mutations:
        with tempfile.TemporaryDirectory(prefix="protodroid-limiter-mutation-") as directory:
            root = Path(directory)
            # Copy only the inputs used by verify, retaining their production relative paths.
            import shutil
            for source in (ROOT / "src/native").glob("*"):
                if source.is_file():
                    destination = root / "src/native" / source.name
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(source, destination)
            target = root / relative
            text = target.read_text(encoding="utf-8")
            if old not in text:
                raise AssertionError(f"mutation input missing: {relative}: {old}")
            target.write_text(text.replace(old, new, 1), encoding="utf-8")
            try:
                verify(root)
            except AssertionError:
                detected += 1
            else:
                raise AssertionError(f"mutation escaped detection: {relative}: {new}")
    return detected


if __name__ == "__main__":
    count = verify()
    print(f"Present-limiter structural checks passed ({count})")
    print(f"Present-limiter mutation checks passed ({mutation_checks()} detected)")
