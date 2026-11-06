#!/usr/bin/env python3
"""Structural checks for opt-in DXVK pipeline outcome/residual diagnostics."""
from pathlib import Path
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: test_dxvk_pipeline_outcomes.py <patched DXVK source root>")
ADAPTER = (Path(sys.argv[1]) / "src/dxvk/dxvk_adapter.cpp").read_text(encoding="utf-8")


def validate(adapter: str) -> int:
    checks = 0

    def check(condition: bool, message: str) -> None:
        nonlocal checks
        checks += 1
        if not condition:
            raise AssertionError(message)

    for field in ("begun", "calls", "inFlight", "pipelines", "succeeded", "compileRequired", "errors",
                  "waitNs", "traceNs", "driverNs", "maxWaitNs", "maxTraceNs", "maxDriverNs"):
        check(f"std::atomic<uint64_t> {field}" in adapter, f"pipeline counter {field}")
    check("const uint64_t inFlight = stats.inFlight.load(std::memory_order_relaxed);" in adapter,
          "in-flight snapshot is not drained as an interval counter")
    for field in ("begun", "calls", "pipelines", "succeeded", "compileRequired", "errors",
                  "waitNs", "traceNs", "driverNs", "maxWaitNs", "maxTraceNs", "maxDriverNs"):
        check(f"stats.{field}.exchange(0, std::memory_order_relaxed)" in adapter,
              f"residual counter drain {field}")
    check('"calls=", calls, " requested=", pipelines, " errors=", errors,' in adapter,
          "existing summary prefix remains analyzer-compatible")
    for field in (" begun=", " in_flight=", " success=", " compile_required=", " trace_avg=", " trace_max="):
        check(f'"{field}"' in adapter, f"summary field {field}")

    begin = adapter.split("static void beginPipelineTiming", 1)[1].split("static void recordPipelineTiming", 1)[0]
    check("stats.begun += 1" in begin and "stats.inFlight += 1" in begin, "attempt/in-flight entry accounting")
    record = adapter.split("static void recordPipelineTiming", 1)[1].split("static bool maliPipelineTraceEnabled", 1)[0]
    check("stats.calls += 1" in record and "stats.inFlight -= 1" in record,
          "completion/in-flight exit accounting")
    check("if (result == VK_SUCCESS)" in record and "stats.succeeded += 1" in record,
          "success outcome accounting")
    check("else if (result == VK_PIPELINE_COMPILE_REQUIRED_EXT)" in record and
          "stats.compileRequired += 1" in record, "compile-required outcome accounting")
    check("else\n      stats.errors += 1" in record, "failure outcome accounting")
    check("stats.traceNs += traced" in record and "stats.driverNs += driven" in record,
          "trace and driver totals remain separate")
    check("waited + traced + driven >= 50000000ull" in record, "exact slow-outcome threshold")
    check('"DXVK pipeline outcome: kind="' in record and '" result="' in record and
          '" wait="' in record and '" trace="' in record and '" driver="' in record and '" total="' in record,
          "slow/failed outcome contains result and all phases")
    check("pCreateInfos" not in record and "SPIR-V" not in record,
          "lightweight outcome logger does not dump pipeline/shader state")

    for kind, name in (("0", "Graphics"), ("1", "Compute")):
        wrapper = adapter.split(f"diagnosticCreate{name}Pipelines(", 1)[1].split("return result;", 1)[0]
        check(f"beginPipelineTiming({kind})" in wrapper, f"{name.lower()} entry accounting")
        check(f"recordPipelineTiming({kind}, createInfoCount, result, lockEnd - callStart,\n"
              "                         driverStart - lockEnd, driverEnd - driverStart)" in wrapper,
              f"{name.lower()} three-phase completion accounting")
        check(f"g_create{name}Pipelines(device, pipelineCache," in wrapper,
              f"real {name.lower()} driver call retained")
        check("const int64_t callStart = timing ? pipelineNowNs() : 0" in wrapper,
              f"{name.lower()} disabled clock gate")
        check("if (serialize)\n      pipelineLock.lock()" in wrapper and
              "if (serialize)\n      pipelineLock.unlock()" in wrapper,
              f"{name.lower()} serialization policy retained")

    destructor = adapter.split("DxvkAdapter::~DxvkAdapter()", 1)[1].split("DxvkAdapterMemoryInfo", 1)[0]
    check("#ifdef __ANDROID__" in destructor and "#endif" in destructor,
          "desktop destructor is unchanged")
    check("if (pipelineTimingEnabled())" in destructor and 'flushPipelineTiming("adapter_destroy")' in destructor,
          "adapter destruction flushes opt-in residuals")
    check('"DXVK pipeline timing final ("' in adapter, "final residual record has a distinct label")
    check('env::getEnvVar("PROTODROID_PIPELINE_TIMING") == "1"' in adapter,
          "outcome diagnostics remain opt-in")
    return checks


def expect_detection(description: str, adapter: str) -> None:
    try:
        validate(adapter)
    except AssertionError:
        return
    raise AssertionError(f"negative control was not detected: {description}")


count = validate(ADAPTER)
expect_detection("in-flight underflow", ADAPTER.replace("stats.inFlight -= 1", "stats.inFlight += 1", 1))
expect_detection("residual flush removed", ADAPTER.replace('flushPipelineTiming("adapter_destroy")', "", 1))
expect_detection("trace folded into driver", ADAPTER.replace("driverStart - lockEnd, driverEnd - driverStart",
                                                             "0, driverEnd - lockEnd", 1))
expect_detection("compile-required mislabeled", ADAPTER.replace("VK_PIPELINE_COMPILE_REQUIRED_EXT", "VK_ERROR_UNKNOWN", 1))
print(f"DXVK pipeline outcome structural checks passed ({count}; 4 negative controls detected)")
