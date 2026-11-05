#!/usr/bin/env python3
"""Structural checks for independently opt-in DXVK draw/state/query attribution."""
from pathlib import Path
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: test_dxvk_submission_timing.py <patched DXVK source root>")
ROOT = Path(__file__).resolve().parents[1]
DXVK = Path(sys.argv[1])
HEADER = (DXVK / "src/d3d9/d3d9_timing.h").read_text(encoding="utf-8")
DEVICE = (DXVK / "src/d3d9/d3d9_device.cpp").read_text(encoding="utf-8")
QUERY = (DXVK / "src/d3d9/d3d9_query.cpp").read_text(encoding="utf-8")
MAIN = (ROOT / "src/native/main.cpp").read_text(encoding="utf-8")
INI = (ROOT / "android/protodroid.ini.example").read_text(encoding="utf-8")
BUILD = (ROOT / "android/build-deps.sh").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/build-apk.yml").read_text(encoding="utf-8")


def validate(header: str, device: str, query: str, main: str, ini: str, build: str, workflow: str) -> int:
    checks = 0

    def check(condition: bool, message: str) -> None:
        nonlocal checks
        checks += 1
        if not condition:
            raise AssertionError(message)

    check('env::getEnvVar("PROTODROID_D3D9_TIMING") == "1"' in header,
          "submission timing has an independent opt-in")
    record = header.split("static void Record(", 1)[1].split("static void MaybeLog", 1)[0]
    check("Logger::" not in record, "per-method Record never logs inside the device mutex")
    check("stats.calls.fetch_add(1, std::memory_order_relaxed)" in record and
          "stats.totalNs.fetch_add(duration, std::memory_order_relaxed)" in record and
          "Max(stats.maxNs, duration)" in record, "relaxed call/total/max recording")
    for marker in ("stats.ok.fetch_add", "stats.falseResults.fetch_add", "stats.errors.fetch_add",
                   "stats.changed.fetch_add", "stats.flagged.fetch_add", "if (units)", "stats.units.fetch_add"):
        check(marker in record, f"record category {marker}")
    check("inclusive nested phases are non-additive" in header and "not GPU time" in header,
          "timing limitations are emitted")
    check("<< \" total_ns=\" << s.totalNs" in header and "<< \" max_ns=\" << s.maxNs" in header,
          "exact integer totals/maxima accompany rounded display values")
    check("Relaxed per-counter snapshots" in header and "std::mutex" not in header,
          "non-transactional residual limitation and no hot-path mutex")
    for field in ("calls", "totalNs", "maxNs", "ok", "falseResults", "errors", "changed", "flagged", "units"):
        check(f"stats.{field}.exchange(0, std::memory_order_relaxed)" in header,
              f"residual snapshot {field}")
    for name in ("draw_lock", "draw_total", "draw_upload", "prepare_draw", "draw_emit",
                 "stream_lock", "stream_total", "indices_lock", "indices_total", "texture_lock", "texture_total",
                 "query_lock", "query_event", "query_occlusion", "query_timestamp", "query_disjoint", "query_other"):
        check(f'"{name}"' in header, f"operation name {name}")

    draw = device.split("D3D9DeviceEx::DrawIndexedPrimitive(", 1)[1].split("D3D9DeviceEx::DrawPrimitiveUP(", 1)[0]
    for op in ("DrawLock", "DrawUpload", "DrawEmit", "DrawTotal"):
        check(f"D3D9Timing::Op::{op}" in draw, f"indexed draw phase {op}")
    check("issuedPrimitives = PrimitiveCount" in draw and "issuedPrimitives);" in draw,
          "draw units are recorded separately")
    check(draw.index("issuedPrimitives = PrimitiveCount") > draw.index("if (unlikely(!PrimitiveCount))"),
          "zero-primitive calls do not contribute issued units")
    check("HRESULT result = [&]() -> HRESULT" in draw and "}();" in draw,
          "draw early returns preserve device-lock scope")
    check(draw.index("}();") < draw.index("D3D9Timing::Op::DrawTotal"),
          "draw total records after lock release")
    check("ctx->drawIndexed(" in draw and "UploadPerDrawData(" in draw and "PrepareDraw(" in draw,
          "real upload/prepare/indexed-draw operations retained")

    prepare = device.split("D3D9DeviceEx::PrepareDraw(", 1)[1].split("D3D9DeviceEx::EnsureSamplerLimit", 1)[0]
    check("prepareTiming ? D3D9Timing::Now() : 0" in prepare and "D3D9Timing::Op::PrepareDraw" in prepare,
          "PrepareDraw has opt-in entry/exit timing")
    for name, op in (("SetStreamSource", "Stream"), ("SetIndices", "Indices"), ("SetStateTexture", "Texture")):
        method = device.split(f"D3D9DeviceEx::{name}(", 1)[1]
        method = method.split("\n  HRESULT", 1)[0]
        check(f"D3D9Timing::Op::{op}Lock" in method and f"D3D9Timing::Op::{op}Total" in method,
              f"{name} lock and total phases")
        check("changed" in method and "recorded" in method, f"{name} changed/recorded categories")
        check("HRESULT result = [&]() -> HRESULT" in method and "}();" in method,
              f"{name} early-return wrapper")
        check(method.index("}();") < method.index(f"D3D9Timing::Op::{op}Total"),
              f"{name} total records after lock release")

    check("D3D9Timing::Op::QueryLock" in query, "query lock wait")
    normalized_query = " ".join(query.split())
    for query_type, op in (("D3DQUERYTYPE_EVENT", "QueryEvent"), ("D3DQUERYTYPE_OCCLUSION", "QueryOcclusion"),
                           ("D3DQUERYTYPE_TIMESTAMP", "QueryTimestamp"),
                           ("D3DQUERYTYPE_TIMESTAMPDISJOINT", "QueryDisjoint")):
        check(f"case {query_type}: queryOp = D3D9Timing::Op::{op}; break;" in normalized_query,
              f"query type mapping {query_type}")
    check("D3D9Timing::Record(queryOp" in query and "result, false, flush" in query,
          "query result and flush-flag recording")
    check("HRESULT result = [&]() -> HRESULT" in query and "}();" in query,
          "query early returns preserve lock scope")
    check(query.index("}();") < query.index("D3D9Timing::Record(queryOp"),
          "query total records after lock release")
    check("this->GetQueryData(pData, dwSize)" in query and "m_parent->ConsiderFlush" in query,
          "real query and implicit-flush behavior retained")

    present = device.split("D3D9DeviceEx::PresentEx(", 1)[1].split("D3D9DeviceEx::CreateRenderTargetEx(", 1)[0]
    check("HRESULT result = m_implicitSwapchain->Present(" in present and "D3D9Timing::MaybeLog()" in present,
          "Present is the periodic logging boundary")
    check(present.index("m_implicitSwapchain->Present(") < present.index("D3D9Timing::MaybeLog()"),
          "periodic logging occurs after Present returns")
    destructor = device.split("D3D9DeviceEx::~D3D9DeviceEx()", 1)[1].split("D3D9DeviceEx::QueryInterface", 1)[0]
    check('D3D9Timing::Flush("device_destroy")' in destructor, "device destruction drains residuals")
    check(destructor.index("waitForIdle()") < destructor.index('D3D9Timing::Flush("device_destroy")'),
          "final flush follows device idle")

    check('SettingInt("d3d9_timing", 0)' in main, "native setting defaults off")
    check('unsetenv("PROTODROID_D3D9_TIMING")' in main, "ordinary Play clears stale tour state")
    check('setenv("PROTODROID_D3D9_TIMING", d3d9_timing ? "1" : "0"' in main,
          "native environment wiring")
    check("diagnostic_tour || SettingInt(\"d3d9_timing\", 0)" in main, "tour opt-in and independent ordinary setting")
    check("#d3d9_timing = 1" in ini and "not use it for a steady FPS baseline" in ini,
          "user-facing overhead/baseline warning")
    check("'DXVK D3D9 timing:'" in build and "'PROTODROID_D3D9_TIMING'" in build,
          "packaged-library markers cover the new diagnostic")
    check("submodule update --init --depth 1 include/native/directx include/vulkan include/spirv" in workflow,
          "CI materializes the pinned compile-test headers")
    check("test_dxvk_submission_timing.py" in workflow and
          "test_dxvk_submission_timing_runtime.py" in workflow and
          "test_dxvk_submission_timing_syntax.py" in workflow and
          "test_analyze_d3d9_timing.py" in workflow,
          "CI runs structural, runtime, syntax, and exact analyzer gates")
    return checks


def expect_detection(description: str, **replacements: str) -> None:
    values = dict(header=HEADER, device=DEVICE, query=QUERY, main=MAIN, ini=INI, build=BUILD, workflow=WORKFLOW)
    values.update(replacements)
    try:
        validate(**values)
    except AssertionError:
        return
    raise AssertionError(f"negative control was not detected: {description}")


count = validate(HEADER, DEVICE, QUERY, MAIN, INI, BUILD, WORKFLOW)
expect_detection("query type mapping removed",
                 query=QUERY.replace("case D3DQUERYTYPE_TIMESTAMP:         queryOp = D3D9Timing::Op::QueryTimestamp; break;", "", 1))
expect_detection("final flush removed", device=DEVICE.replace('D3D9Timing::Flush("device_destroy")', "", 1))
expect_detection("logging moved into Record",
                 header=HEADER.replace("stats.calls.fetch_add", "Logger::info(\"bad\"); stats.calls.fetch_add", 1))
expect_detection("steady-baseline warning removed", ini=INI.replace("not use it for a steady FPS baseline", "baseline-safe", 1))
expect_detection("default enabled", main=MAIN.replace('SettingInt("d3d9_timing", 0)', 'SettingInt("d3d9_timing", 1)', 1))
expect_detection("CI header submodules removed",
                 workflow=WORKFLOW.replace(" include/native/directx include/vulkan include/spirv", "", 1))
print(f"DXVK submission timing structural checks passed ({count}; 6 negative controls detected)")
