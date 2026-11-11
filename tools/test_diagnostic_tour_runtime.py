#!/usr/bin/env python3
"""Read-only binary probe and source-policy checks for the diagnostic tour.

The binary probe proves that the supported engine's InGameState vtables use recompiled thunks which bypass the old
computed-dispatch-only probe. The source checks keep normal Play on EngineTerminate while ending isolated tours at the
main-loop boundary. Neither check is a substitute for a physical gameplay run.
"""
from hashlib import sha256
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x10000000
DLL = ROOT / "prototypeenginef.dll"
data = DLL.read_bytes()
assert sha256(data).hexdigest() == "d331be205bdde99897952ee88261ab7e63c5c65e36a4660e2a367886a3537071", (
    "The tour address probe requires Steam build 19788432"
)


def u32(va: int) -> int:
    # In this identified DLL, these section file offsets equal VA - ImageBase.
    return struct.unpack_from("<I", data, va - BASE)[0]


def direct_jump_target(va: int) -> int:
    offset = va - BASE
    assert data[offset] == 0xE9, f"{va:#x} is not the expected near jump thunk"
    displacement = struct.unpack_from("<i", data, offset + 1)[0]
    return va + 5 + displacement


# Two InGameState vtable entries in .rdata and the tasks behind their recompiled thunks.
assert u32(0x10E1A7D0) == 0x100246B3
assert u32(0x10E1A7DC) == 0x1002BC01
assert direct_jump_target(0x100246B3) == 0x104E45C0
assert direct_jump_target(0x1002BC01) == 0x104E4AD0

# The generated thunks take the Android direct path rather than re-entering RecompDispatch for the final task.
generated = (ROOT / "build/recomp/native/part_0002.c").read_text(encoding="utf-8")
for thunk, task in ((0x7C0246B3, 0x7C4E45C0), (0x7C02BC01, 0x7C4E4AD0)):
    body = generated.split(f"void f_{thunk:08x}(Ctx* c) {{", 1)[1].split("\n}", 1)[0]
    assert f"if (RecompDirect(0x{task:08x}u))" in body
runtime_header = (ROOT / "src/recomp/runtime.h").read_text(encoding="utf-8")
flat_direct = runtime_header.split("#ifdef RECOMP_FLAT_MEMORY", 1)[1].split("#else", 1)[0]
assert "return 1;" in flat_direct, "Android's direct-thunk assumption changed; review the tour probe"

runtime = (ROOT / "src/native/runtime.cpp").read_text(encoding="utf-8")
dispatch = runtime.split('extern "C" void RecompDispatch(Ctx* ctx, uint32_t target)', 1)[1]
dispatch = dispatch.split('extern "C" void RecompCpuid', 1)[0]
assert dispatch.index("DiagnosticTourGameplayTaskTarget(target, kEngineBase)") < dispatch.index(
    "RecompLookup(target)"
), "The vtable target must be classified before its recompiled thunk bypasses FollowThunks"
assert dispatch.index("FollowThunks(target)") < dispatch.index(
    "DiagnosticTourGameplayTaskTarget(real, kEngineBase)"
), "Resolved direct task targets must retain probe coverage"

main = (ROOT / "src/native/main.cpp").read_text(encoding="utf-8")
tour_exit = main.rsplit("    if (diagnostic_tour) {", 1)[1].split(
    "    // Ordinary Play retains the engine's normal termination path.", 1
)[0]
assert "DiagnosticTourFailed()" in tour_exit and "StartupFailure(" in tour_exit
assert "ReleaseInjectedInput();" in tour_exit
assert "VulkanDriverAbandon();" in tour_exit
assert "DiagnosticsStop();" in tour_exit and "MirrorCopyDXVKLogs();" in tour_exit
assert "_exit(0);" in tour_exit
assert "GuestCall(terminate" not in tour_exit, "Isolated tours must not repeat the observed EngineTerminate hang"
ordinary_exit = main.split("// Ordinary Play retains the engine's normal termination path.", 1)[1]
assert "GuestCall(terminate, {});" in ordinary_exit, "Normal Play must retain EngineTerminate"

watchdog = (ROOT / "src/native/watchdog.cpp").read_text(encoding="utf-8")
assert "if (now == g_frames->load()) DiagnosticTourWatchdogStalled(now, seconds);" in watchdog, (
    "The watchdog must recheck progress after backtrace capture before ending a tour"
)
tour_source = (ROOT / "src/native/diagnostic_tour.cpp").read_text(encoding="utf-8")
plan_source = (ROOT / "src/native/diagnostic_tour_plan.h").read_text(encoding="utf-8")
enum_body = plan_source.split("enum class TourInput : uint8_t {", 1)[1].split("};", 1)[0]
for input_name in re.findall(r"^\s*([A-Z][A-Za-z0-9]*),\s*$", enum_body, re.MULTILINE):
    if input_name == "None":
        continue
    expected = 1 if input_name in {"LeftMouse", "RightMouse"} else 2
    actual = tour_source.count(f"case TourInput::{input_name}:")
    assert actual == expected, f"{input_name} has {actual} name/scancode mappings; expected {expected}"
watchdog_exit = tour_source.split("void DiagnosticTourWatchdogStalled(uint64_t frame, int seconds)", 1)[1]
watchdog_exit = watchdog_exit.split("void DiagnosticTourCrashed", 1)[0]
assert "if (!g_started.load(std::memory_order_acquire)) return;" in watchdog_exit, (
    "The tour watchdog exit must remain inactive during ordinary Play"
)
assert "DiagnosticTourFail(" in watchdog_exit and "_exit(71);" in watchdog_exit
assert "ReleaseInjectedInput" not in watchdog_exit and "GuestCall" not in watchdog_exit, (
    "The watchdog thread must not call guest input/engine code"
)
assert "TourActionStepsForPresentBatch(batch)" in tour_source, (
    "The controller must apply the tested one-action-frame policy to coalesced Presents"
)
assert "for (uint64_t i = 0; i < batch" not in tour_source, (
    "Never compress a multi-Present batch into multiple inputs after one EngineService"
)
assert "presented_frames=%llu action_frames=%u" in tour_source, (
    "Actual Present dwell and logical action frames must remain separately observable"
)
assert "exit_behavior=intentional-isolated-process-close" in tour_source, (
    "The durable result must distinguish the expected tour close from a native crash"
)
for evidence in (
    "actions_executed=%llu", "wall_ms=%.3f", "process_cpu_ms=%.3f",
    "process_cpu_utilization_pct=%.2f", "present_window=last-completed-rolling-window",
    "present_window_p99_ms=%.3f", "gameplay_dispatches=%u",
    "guest_clock_scale=%u", "phase_present_intervals=%llu", "phase_present_average_ms=%.3f",
    "phase_present_max_ms=%.3f", "import_profile=enabled",
):
    assert evidence in tour_source, f"Missing durable tour evidence: {evidence}"
begin_phase = tour_source.split("void BeginPhase(size_t index)", 1)[1].split("void CompletePhase()", 1)[0]
assert 'status=requested coverage=%s", true' not in begin_phase, "Duplicate durable requested/running records returned"
assert 'status=running minimum_present_frames=%u' in begin_phase
assert 'false, phase.name, index, phase.frames' in begin_phase, "Running record must use the completion/failure sync"
assert 'WriteMarker(g_result_path, marker, false)' in tour_source, "Final same-directory markers should share one directory sync"
assert 'if (sync_directory) SyncLogDirectory();' in tour_source
assert 'actions != phase.action_count' in tour_source, "Completed phases must verify exact executed-action counts"
assert 'phase_metrics=not-started' in tour_source, "Startup failures must not inherit stale phase metrics"
assert 'ResetDetailedPresentTotals();' in tour_source, "Each phase needs independent exact Present totals"
assert 'kTourTimeoutNs = 20ll * 60' in tour_source, "The longer tour needs its reviewed failure deadline"

clock_header = (ROOT / "src/native/guest_time.h").read_text(encoding="utf-8")
assert 'kDiagnosticTourGuestClockScale = 4' in clock_header
assert 'if (!diagnostic_tour) return elapsed_ns;' in clock_header, "Normal Play must retain real guest clocks"
assert 'SetGuestClockDiagnosticTour(diagnostic_tour);' in main, "Each native entry must reset the guest-clock mode"
assert 'static const bool diagnostic_tour = DiagnosticTourEnabled();' not in clock_header, (
    "A one-time clock-mode cache can leak 4x time into a later ordinary activity"
)
for source_name in ("kernel32.cpp", "misc.cpp", "bink.cpp"):
    source = (ROOT / "src/native" / source_name).read_text(encoding="utf-8")
    assert 'GuestElapsedNanos(ns)' in source, f"{source_name} is outside the reviewed tour clock policy"
assert 'GetSystemTimeAsFileTime' not in clock_header, "Wall-clock APIs must not be accelerated"
assert 'setenv("PROTODROID_IMPORT_PROFILE", "1", 1);' in main, "The tour must collect guest/import attribution"
assert main.index('setenv("PROTODROID_IMPORT_PROFILE", "1", 1);') < main.index('native::DiagnosticTourStart('), (
    "The journal must not claim import profiling before its override is active"
)
assert 'unsetenv("PROTODROID_IMPORT_PROFILE");' in main, "Normal Play must not inherit the tour profiling override"
assert 'diagnostic_tour ? kDiagnosticTourCommandLine : ""' in main, "Normal Play lost its original command line"

print("Diagnostic-tour binary probe, 4x guest-clock, long-plan, isolated-exit, and watchdog checks passed")
