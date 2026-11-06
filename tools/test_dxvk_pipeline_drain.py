#!/usr/bin/env python3
"""Structural gates for the dormant bounded two-stage warm-cache drain primitive."""
from pathlib import Path
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: test_dxvk_pipeline_drain.py <patched DXVK source root>")
ROOT = Path(__file__).resolve().parents[1]
DXVK = Path(sys.argv[1])
BUILD = (ROOT / "android/build-deps.sh").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/build-apk.yml").read_text(encoding="utf-8")
HEADER = (DXVK / "src/dxvk/dxvk_pipeline_drain.h").read_text(encoding="utf-8")
MANAGER_H = (DXVK / "src/dxvk/dxvk_pipemanager.h").read_text(encoding="utf-8")
MANAGER = (DXVK / "src/dxvk/dxvk_pipemanager.cpp").read_text(encoding="utf-8")
CACHE_H = (DXVK / "src/dxvk/dxvk_state_cache.h").read_text(encoding="utf-8")
CACHE = (DXVK / "src/dxvk/dxvk_state_cache.cpp").read_text(encoding="utf-8")
DEVICE = (DXVK / "src/dxvk/dxvk_device.cpp").read_text(encoding="utf-8")
D3D9 = (DXVK / "src/d3d9/d3d9_device.cpp").read_text(encoding="utf-8")


def validate(header: str, manager_h: str, manager: str, cache_h: str, cache: str, device: str, d3d9: str,
             build: str, workflow: str) -> int:
    count = 0

    def check(condition: bool, message: str) -> None:
        nonlocal count
        count += 1
        if not condition:
            raise AssertionError(message)

    check("stateCachePending" in header and "pipelinePending()" in header, "two-stage pending snapshot")
    check("pipelineTasksTotal > pipelineTasksCompleted" in header, "saturating task subtraction")
    check("stateCachePending == 0 && pipelinePending() == 0" in header, "idle requires both stages")
    check("kMaximumWaitNs = 30000000000ull" in header, "30-second bound")
    check("kPollIntervalNs = 1000000ull" in header, "bounded one-millisecond poll")
    check("std::min(timeoutNs, kMaximumWaitNs)" in header, "timeout clamp")
    check("now >= deadline" in header and "remaining" in header, "deadline and partial final sleep")
    check("result.stats = readStats()" in header and "result.iterations += 1" in header,
          "each poll takes a fresh result snapshot")
    check("No production caller" in MANAGER_H and "must not hold the D3D9 device lock" in MANAGER_H,
          "dormant caller and mutex contract")
    check("DxvkPipelineDrainStats getWarmCacheStats()" in MANAGER_H and
          "DxvkPipelineDrainResult waitForWarmCache(uint64_t timeoutNs)" in MANAGER_H,
          "manager API")
    check("m_stateCache.getWorkerPending()" in manager and "workers.tasksTotal" in manager and
          "workers.tasksCompleted" in manager, "manager combines feeder and pipeline counters")
    wait = manager.split("DxvkPipelineManager::waitForWarmCache", 1)[1].split("stopWorkerThreads", 1)[0]
    check("DxvkPipelineDrain::Wait<std::chrono::steady_clock>" in wait, "real steady-clock wait")
    check("std::this_thread::sleep_for(delay)" in wait, "sleep occurs outside counter locks")
    check("LockDevice" not in wait and "vkDeviceWaitIdle" not in wait and "waitForIdle" not in wait,
          "warm wait does not take device/GPU idle paths")
    check("DXVK warm-cache drain:" in wait and "result.stats.pipelinePending()" in wait,
          "explicit call logs outcome and residuals")

    check("uint64_t getWorkerPending()" in CACHE_H and "m_workerActive = false" in CACHE_H,
          "feeder pending API and active state")
    pending = cache.split("DxvkStateCache::getWorkerPending()", 1)[1].split("DxvkStateCache::stopWorkers", 1)[0]
    check("std::lock_guard<dxvk::mutex> lock(m_workerLock)" in pending and
          "m_workerQueue.size()" in pending and "m_workerActive ? 1u : 0u" in pending,
          "locked queued-plus-active count")
    worker = cache.split("void DxvkStateCache::workerFunc()", 1)[1].split("void DxvkStateCache::writerFunc()", 1)[0]
    check("m_workerActive = true" in worker and "m_workerActive = false" in worker,
          "worker active transitions exist")
    check(worker.index("m_workerQueue.pop()") < worker.index("m_workerActive = true"),
          "active flag is set with the popped item")
    check(worker.index("compilePipelines(item)") < worker.index("m_workerActive = false"),
          "active flag clears only after task enqueue returns")
    check("m_workerActive = false" not in worker.split("compilePipelines(item)", 1)[0].split("m_workerQueue.pop()", 1)[1],
          "active flag is not cleared before compilePipelines")
    check("return m_objects.pipelineManager().waitForWarmCache(timeoutNs)" in device,
          "device forwards without GPU wait")
    check("waitForWarmCache" not in d3d9, "no D3D9/Present production caller")
    check("compileGraphicsPipeline" not in header and "compilePipelineLibrary" not in header,
          "drain primitive invents no work")
    check("'DXVK warm-cache drain:'" in build, "packaged-library drain marker")
    check("test_dxvk_pipeline_drain.py" in workflow and "test_dxvk_pipeline_drain_runtime.py" in workflow and
          "test_dxvk_pipeline_drain_syntax.py" in workflow, "CI runs all drain gates")
    return count


def expect_detection(description: str, **changes: str) -> None:
    values = dict(header=HEADER, manager_h=MANAGER_H, manager=MANAGER, cache_h=CACHE_H,
                  cache=CACHE, device=DEVICE, d3d9=D3D9, build=BUILD, workflow=WORKFLOW)
    values.update(changes)
    try:
        validate(**values)
    except AssertionError:
        return
    raise AssertionError(f"negative control was not detected: {description}")


total = validate(HEADER, MANAGER_H, MANAGER, CACHE_H, CACHE, DEVICE, D3D9, BUILD, WORKFLOW)
expect_detection("timeout removed", header=HEADER.replace("kMaximumWaitNs = 30000000000ull",
                                                           "kMaximumWaitNs = 0ull", 1))
expect_detection("feeder active race reopened", cache=CACHE.replace("m_workerActive = true;", "", 1))
expect_detection("production Present caller", d3d9=D3D9 + "\n// waitForWarmCache(1000)\n")
expect_detection("GPU wait introduced", manager=MANAGER.replace("std::this_thread::sleep_for(delay)",
                                                                "vkDeviceWaitIdle(); std::this_thread::sleep_for(delay)", 1))
expect_detection("device lock introduced", manager=MANAGER.replace("DxvkPipelineDrain::Wait<std::chrono::steady_clock>",
                                                                  "LockDevice(); DxvkPipelineDrain::Wait<std::chrono::steady_clock>", 1))
expect_detection("packaged marker removed", build=BUILD.replace("'DXVK warm-cache drain:'", "", 1))
print(f"DXVK pipeline-drain structural checks passed ({total}; 6 negative controls detected)")
