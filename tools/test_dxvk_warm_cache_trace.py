#!/usr/bin/env python3
"""Structural and mutation checks for opt-in nonblocking warm-cache snapshots."""
from pathlib import Path
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: test_dxvk_warm_cache_trace.py <patched DXVK source root>")
ROOT = Path(__file__).resolve().parents[1]
DXVK = Path(sys.argv[1]).resolve()
SOURCES = {
    "header": (DXVK / "src/d3d9/d3d9_warm_cache_trace.h").read_text(encoding="utf-8"),
    "present_all": (DXVK / "src/d3d9/d3d9_device.cpp").read_text(encoding="utf-8"),
    "state": (DXVK / "src/dxvk/dxvk_state_cache.cpp").read_text(encoding="utf-8"),
    "pipe": (DXVK / "src/dxvk/dxvk_pipemanager.cpp").read_text(encoding="utf-8"),
    "device": (DXVK / "src/dxvk/dxvk_device.cpp").read_text(encoding="utf-8"),
    "drain": (DXVK / "src/dxvk/dxvk_pipeline_drain.h").read_text(encoding="utf-8"),
    "main": (ROOT / "src/native/main.cpp").read_text(encoding="utf-8"),
    "ini": (ROOT / "android/protodroid.ini.example").read_text(encoding="utf-8"),
    "workflow": (ROOT / ".github/workflows/build-apk.yml").read_text(encoding="utf-8"),
    "build": (ROOT / "android/build-deps.sh").read_text(encoding="utf-8"),
}


def validate(**changes: str) -> int:
    values = dict(SOURCES)
    values.update(changes)
    header = values["header"]
    present_all = values["present_all"]
    state = values["state"]
    pipe = values["pipe"]
    device = values["device"]
    drain = values["drain"]
    main = values["main"]
    ini = values["ini"]
    workflow = values["workflow"]
    build = values["build"]
    checks = 0

    def check(condition: bool, _description: str) -> None:
        nonlocal checks
        if not condition:
            raise AssertionError(_description)
        checks += 1

    check('env::getEnvVar("PROTODROID_WARM_CACHE_TRACE") == "1"' in header, "explicit opt-in environment")
    check("kIntervalNs = 250000000ll" in header, "250 ms snapshot cadence")
    disabled = header.index("if (!enabled)")
    check(disabled < header.index("const int64_t now = clock()") <
          header.index("state.nextLogNs.load"), "disabled polls precede clock and scheduler reads")
    check("compare_exchange_weak(next, NextDue(now)" in header, "single due-window claimant")
    check("emit(readSnapshot(), now)" in header, "timestamped read only for the due claimant")
    check("std::numeric_limits<int64_t>::max() - kIntervalNs" in header, "due-time overflow saturation")
    check("sleep" not in header and "waitForWarmCache" not in header, "scheduler never waits or drains")

    check("struct DxvkWarmCacheSnapshot" in drain and "bool stateCacheKnown = false" in drain,
          "snapshot has an explicit feeder-validity bit")
    check("numeric stateCachePending field is unknown" in drain, "unknown is not documented as zero")
    try_method = state.split("bool DxvkStateCache::tryGetWorkerPending", 1)[1].split(
        "void DxvkStateCache::stopWorkers", 1)[0]
    check("std::unique_lock<dxvk::mutex> lock(m_workerLock, std::try_to_lock)" in try_method,
          "feeder snapshot uses try-lock")
    check(try_method.index("if (!lock.owns_lock())") < try_method.index("return false") <
          try_method.index("pending ="), "contention returns before writing pending")
    check("m_workerQueue.size()) + (m_workerActive ? 1u : 0u)" in try_method,
          "queued and active feeder work are both sampled")
    exact = state.split("uint64_t DxvkStateCache::getWorkerPending", 1)[1].split(
        "bool DxvkStateCache::tryGetWorkerPending", 1)[0]
    check("std::lock_guard<dxvk::mutex> lock(m_workerLock)" in exact,
          "exact drain read retains its locking behavior")

    snapshot = pipe.split("DxvkWarmCacheSnapshot DxvkPipelineManager::getWarmCacheSnapshot", 1)[1].split(
        "DxvkPipelineDrainResult DxvkPipelineManager::waitForWarmCache", 1)[0]
    check("tryGetWorkerPending(result.stats.stateCachePending)" in snapshot, "manager propagates feeder unknowns")
    check("result.stats.pipelineTasksTotal = workers.tasksTotal" in snapshot and
          "result.stats.pipelineTasksCompleted = workers.tasksCompleted" in snapshot,
          "manager preserves raw pipeline counters")
    check("waitForWarmCache" not in snapshot and "getWarmCacheStats" not in snapshot,
          "snapshot cannot accidentally invoke the exact drain path")
    check("DxvkWarmCacheSnapshot DxvkDevice::getWarmCacheSnapshot" in device and
          "pipelineManager().getWarmCacheSnapshot()" in device, "device forwards only the snapshot")

    present = present_all.split("D3D9DeviceEx::PresentEx(", 1)[1].split(
        "D3D9DeviceEx::CreateRenderTargetEx(", 1)[0]
    check("m_dxvkDevice->getWarmCacheSnapshot()" in present, "Present supplies the real snapshot reader")
    check(present.index("m_implicitSwapchain->Present(") < present.index("D3D9Timing::MaybeLog()") <
          present.index("D3D9WarmCacheTrace::MaybeLog") < present.index("return result"),
          "snapshot logging follows actual Present and existing timing outside the device lock")
    check("static D3D9WarmCacheTrace::State warmCacheTraceState" in present, "shared atomic scheduler state")
    check("waitForWarmCache" not in present_all, "no D3D9 drain production caller")
    for field in ("at_ns", "state_known", "state_pending", "pipeline_total", "pipeline_completed", "pipeline_pending"):
        check(f'{field}="' in present, f"raw log field {field}")
    check('"DXVK warm-cache snapshot: at_ns="' in present, "stable log marker")

    check('SettingInt("warm_cache_trace", 0)' in main, "native setting defaults off")
    check('unsetenv("PROTODROID_WARM_CACHE_TRACE")' in main, "ordinary launch clears stale process state")
    check('setenv("PROTODROID_WARM_CACHE_TRACE", warm_cache_trace ? "1" : "0", 1)' in main,
          "native setting deterministically controls both directions")
    check('diagnostic_tour || SettingInt("warm_cache_trace"' not in main, "tour does not change prior capture policy")
    check("#warm_cache_trace = 1" in ini and "state_known=0 means feeder count is unknown" in ini and
          "Never waits or drains" in ini and "not an FPS baseline" in ini, "user-facing semantics and overhead warning")
    check("'DXVK warm-cache snapshot:'" in build and "'PROTODROID_WARM_CACHE_TRACE'" in build,
          "packaged-library markers cover snapshot diagnostics")
    check("test_dxvk_warm_cache_trace.py" in workflow and
          "test_dxvk_warm_cache_trace_runtime.py" in workflow and
          "test_dxvk_warm_cache_trace_syntax.py" in workflow and
          "test_analyze_warm_cache_trace.py" in workflow,
          "CI runs structural, runtime, syntax, and analyzer gates")
    return checks


count = validate()
expect_mutations = [
    ("disabled fast path removed", "header", "if (!enabled)\n        return;", ""),
    ("try-lock downgraded to waiting lock", "state",
     "std::unique_lock<dxvk::mutex> lock(m_workerLock, std::try_to_lock)",
     "std::lock_guard<dxvk::mutex> lock(m_workerLock)"),
    ("raw pipeline totals removed", "pipe", "result.stats.pipelineTasksTotal = workers.tasksTotal;", ""),
    ("drain installed in Present", "present_all", "D3D9WarmCacheTrace::MaybeLog(warmCacheTraceState",
     "m_dxvkDevice->waitForWarmCache(1); D3D9WarmCacheTrace::MaybeLog(warmCacheTraceState"),
    ("default enabled", "main", 'SettingInt("warm_cache_trace", 0)', 'SettingInt("warm_cache_trace", 1)'),
    ("tour behavior changed", "main", 'SettingInt("warm_cache_trace", 0)',
     'diagnostic_tour || SettingInt("warm_cache_trace", 0)'),
    ("unknown semantics hidden", "ini", "state_known=0 means feeder count is unknown", "state_known=0 means idle"),
    ("CI runtime gate removed", "workflow", "test_dxvk_warm_cache_trace_runtime.py", ""),
]
for description, key, old, new in expect_mutations:
    try:
        validate(**{key: SOURCES[key].replace(old, new, 1)})
    except (AssertionError, ValueError):
        continue
    raise AssertionError(f"negative control was not detected: {description}")
print(f"DXVK warm-cache trace structural checks passed ({count}; {len(expect_mutations)} negative controls detected)")
