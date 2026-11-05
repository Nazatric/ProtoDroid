#!/usr/bin/env python3
"""Structural boundaries for worker-only raw Present interval persistence."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIAGNOSTICS = (ROOT / "src/native/diagnostics.cpp").read_text(encoding="utf-8")
PRESENT = (ROOT / "src/native/present_stats.cpp").read_text(encoding="utf-8")
D3D9 = (ROOT / "src/native/d3d9.cpp").read_text(encoding="utf-8")
TRACE = (ROOT / "src/native/frame_interval_trace.h").read_text(encoding="utf-8")
INI = (ROOT / "android/protodroid.ini.example").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/build-apk.yml").read_text(encoding="utf-8")


def validate(diagnostics: str, present: str, d3d9: str, trace: str, ini: str, workflow: str) -> int:
    count = 0

    def check(condition: bool, message: str) -> None:
        nonlocal count
        count += 1
        if not condition:
            raise AssertionError(message)

    check('SettingInt("frame_interval_trace", 0)' in diagnostics, "raw trace defaults off")
    check("tour || SettingInt(\"frame_interval_trace\", 0)" in diagnostics, "tour enables raw trace")
    check("tour || frame_trace || SettingInt(\"detailed_diagnostics\"" in diagnostics,
          "raw trace implies detailed sample collection")
    check("SetPresentDetailedEnabled(detailed)" in diagnostics, "Present collector follows detailed state")
    check("FrameIntervalTrace frame_trace(g_frame_interval_trace, FrameIntervalTracePath())" in diagnostics,
          "worker constructs the trace")
    check('getenv("DXVK_LOG_PATH")' in diagnostics and "proto-frame-intervals.csv" in diagnostics,
          "trace uses the visible per-run log directory")
    emit = diagnostics.split("void EmitWindow(", 1)[1].split("void DiagnosticsWorker()", 1)[0]
    check("frame_trace.Write(present)" in emit, "window persists raw samples")
    check(emit.index("frame_trace.Write(present)") < emit.index("TimingSummary(present)"),
          "raw samples persist before percentile sorting")
    check("was_capped" in emit and "previous_trace_error" in emit, "cap/error transitions are reported once")
    worker = diagnostics.split("void DiagnosticsWorker()", 1)[1].split("void RunDiagnosticsWorker()", 1)[0]
    check('frame_trace.Close("final")' in worker, "final close exists")
    check(worker.index("EmitWindow(health, cpu, run, last_thermal, last_emit_ns, frame_trace, true)") <
          worker.index('frame_trace.Close("final")'), "final residual drains before close")
    check(worker.index('frame_trace.Close("final")') < worker.index("LogRunSummary(run)"),
          "trace closes before the run summary")
    check("g_frame_interval_trace = false" in diagnostics, "lifecycle resets trace state")
    check("FrameIntervalTrace" not in present and "frame_interval" not in present,
          "Present hot path has no trace file work")
    check("FrameIntervalTrace" not in d3d9 and "proto-frame-intervals" not in d3d9,
          "D3D9 Present path is unchanged")

    check("sequence,present_end_ns,interval_ns" in trace, "versioned raw columns")
    check("kDefaultMaxBytes = 64ull * 1024 * 1024" in trace, "64 MiB bound")
    check("size > max_bytes_ || bytes_ > max_bytes_ - size" in trace, "overflow-safe byte cap")
    check("snapshot.count != snapshot.samples.size()" in trace and
          "snapshot.total_ns != uint64_t(snapshot.end_ns - snapshot.begin_ns)" in trace,
          "window consistency checks")
    check("rotation failed" in trace and "Never truncate" in trace, "failed rotation preserves old capture")
    write = trace.split("void Write(const TimingWindowSnapshot& snapshot)", 1)[1].split("void Close(", 1)[0]
    check(write.count("std::fflush") == 1 and write.index("for (int64_t duration_ns") < write.index("std::fflush"),
          "flush is per window rather than per row")
    check("fsync(" not in write, "measurement path does not call fsync")
    check("# closed=" in trace and "rows_" in trace, "final trailer and exact row count")
    check("#frame_interval_trace = 1" in ini and "64 MiB cap" in ini, "user-facing opt-in and cap")
    check("frame_interval_trace_test.cpp" in workflow and "/tmp/frame_interval_trace_test" in workflow,
          "CI compiles and runs the real writer")
    return count


def expect_detection(description: str, **changes: str) -> None:
    values = dict(diagnostics=DIAGNOSTICS, present=PRESENT, d3d9=D3D9, trace=TRACE, ini=INI, workflow=WORKFLOW)
    values.update(changes)
    try:
        validate(**values)
    except AssertionError:
        return
    raise AssertionError(f"negative control was not detected: {description}")


total = validate(DIAGNOSTICS, PRESENT, D3D9, TRACE, INI, WORKFLOW)
expect_detection("default enabled", diagnostics=DIAGNOSTICS.replace('SettingInt("frame_interval_trace", 0)',
                                                                    'SettingInt("frame_interval_trace", 1)', 1))
expect_detection("write after percentile sort",
                 diagnostics=DIAGNOSTICS.replace("frame_trace.Write(present);", "", 1))
expect_detection("hot-path file integration", present=PRESENT + '\n#include "frame_interval_trace.h"\n')
expect_detection("byte cap removed", trace=TRACE.replace("size > max_bytes_ || bytes_ > max_bytes_ - size", "false", 1))
expect_detection("final close removed", diagnostics=DIAGNOSTICS.replace('frame_trace.Close("final")', "", 1))
print(f"Frame interval trace structural checks passed ({total}; 5 negative controls detected)")
