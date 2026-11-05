#!/usr/bin/env python3
"""Structural checks for opt-in rect-lock attribution; not a device timing or rendering test."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
D3D9 = (ROOT / "src/native/d3d9.cpp").read_text(encoding="utf-8")
DIAGNOSTICS = (ROOT / "src/native/diagnostics.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "src/native/operation_timing.h").read_text(encoding="utf-8")


def validate(d3d9: str, diagnostics: str, header: str) -> int:
    checks = 0

    def check(condition: bool, message: str) -> None:
        nonlocal checks
        checks += 1
        if not condition:
            raise AssertionError(message)

    check("constexpr int kDetailedDiagnosticsDefault = 0" in (ROOT / "src/native/diagnostics.h").read_text(),
          "detailed diagnostics remain opt-in")
    check("struct OperationTiming" in header and "OperationTiming Take()" in header,
          "shared operation counter and residual snapshot")
    check("if (ns > kThresholds[i])" in header, "strict operation thresholds")
    check("timing.Add(DurationNanos(elapsed))" in d3d9, "bookkeeping uses shared counter")
    check("std::lock_guard<std::mutex> lock(g_texture_timing_mutex);\n    timing.Add" in d3d9,
          "bookkeeping mutex is outside the measured helper")

    timed_lock = d3d9.split("HRESULT TimedRectLock(Lock lock) {", 1)[1].split("\n}", 1)[0]
    check("if (!DetailedDiagnosticsEnabled()) return lock();" in timed_lock,
          "disabled rect timing forwards directly")
    check(timed_lock.index("HRESULT hr = lock();") < timed_lock.index("RecordTextureTiming"),
          "host lock is recorded only after return")
    check("g_rect_host_timing" in timed_lock, "host lock has a distinct phase")

    shadow_lock = d3d9.split("uint32_t ShadowLock(", 1)[1].split("\nvoid ShadowUnlock(", 1)[0]
    shadow_unlock = d3d9.split("void ShadowUnlock(", 1)[1].split("\nvoid ForgetShadows(", 1)[0]
    for name, source in (("lock", shadow_lock), ("unlock", shadow_unlock)):
        check("rect && DetailedDiagnosticsEnabled()" in source,
              f"shadow {name} timing is both rect-scoped and opt-in")
        check("g_rect_wait_timing" in source, f"shadow {name} mutex wait is measured")
    for marker in ("g_rect_alloc_timing", "g_rect_copy_in_timing"):
        check(marker in shadow_lock, f"shadow lock phase {marker}")
    for marker in ("g_rect_adapt_timing", "g_rect_copy_back_timing", "g_rect_free_timing"):
        check(marker in shadow_unlock, f"shadow unlock phase {marker}")

    # Only the two plain rect shadow-lock routes opt into rect accounting. Buffer and box routes retain defaults.
    check("ShadowLock(object, sub, lr.pBits, size, flags, rect == nullptr, true)" in d3d9,
          "LockRectCommon rect scope")
    check("ShadowLock(tex, sub, lr.pBits, uint32_t(lr.Pitch) * rows, flags, rect == nullptr, true)" in d3d9,
          "adapted plain rect scope")
    check("ShadowLock(object, sub, lb.pBits, uint32_t(lb.SlicePitch) * depth, flags)" in d3d9,
          "volume/box locks do not feed rect counters")
    check("ShadowLock(b, 0, p, size, flags)" in d3d9, "buffer locks do not feed rect counters")
    check("ShadowUnlock(b, 0);" in d3d9 and "ShadowUnlock(v, 0);" in d3d9,
          "buffer/volume unlocks do not feed rect counters")
    for marker in (
        "ShadowUnlock(s, 0, nullptr, false, true)",
        "ShadowUnlock(t, level, &guest_desc, a != nullptr, true)",
        "ShadowUnlock(t, level, nullptr, a != nullptr, true)",
        "ShadowUnlock(t, face * 64 + level, nullptr, a != nullptr, true)",
    ):
        check(marker in d3d9, f"rect unlock scope: {marker}")

    # Every guest rect host call, including adapted lambdas, uses the same post-call host timer.
    check(d3d9.count("TimedRectLock([&]") == 5, "all five surface/texture/cube host rect calls are timed")
    for marker in (
        "s->LockRect(&lr, rect, Arg(c, 3))",
        "t->LockRect(host_level, lr, rect, Arg(c, 4))",
        "t->LockRect(level, &lr, rect, Arg(c, 4))",
        "t->LockRect(D3DCUBEMAP_FACES(face), host_level, lr, rect, Arg(c, 5))",
        "t->LockRect(D3DCUBEMAP_FACES(face), level, &lr, rect, Arg(c, 5))",
    ):
        check(marker in d3d9, f"host rect operation retained: {marker}")

    adapted_lock = d3d9.split("HRESULT AdaptedLockRect(", 1)[1].split("\n// Returns true", 1)[0]
    adapted_unlock = d3d9.split("bool AdaptedUnlock(", 1)[1].split("\nvoid ForgetResource(", 1)[0]
    check("g_rect_wait_timing" in adapted_lock and "g_rect_alloc_timing" in adapted_lock,
          "decoded/dropped adapted lock wait and allocation")
    check("g_rect_wait_timing" in adapted_unlock and "g_rect_adapt_timing" in adapted_unlock and
          "g_rect_free_timing" in adapted_unlock, "adapted unlock phases")
    check("RecordTextureTiming(g_decode_timing" in adapted_unlock, "existing DXT decode timing retained")
    check("TimedTextureUpload" in d3d9 and "g_upload_timing" in d3d9,
          "existing host unlock/upload timing retained")

    check("std::string TakeLockTimingStats()" in d3d9, "rect stats snapshot exists")
    check("taken[i] = timings[i]->Take()" in d3d9, "rect residual counters are drained")
    for name in ("rect_host", "rect_wait", "rect_alloc", "rect_copy_in", "rect_adapt", "rect_copy_back", "rect_free"):
        check(f'"{name}"' in d3d9, f"rect output field {name}")
    check('Log("diag: rect locks %s", TakeLockTimingStats().c_str())' in diagnostics,
          "periodic/final diagnostics emit rect residuals")
    check('Log("diag: texture %s", TakeTextureTimingStats().c_str())' in diagnostics,
          "existing texture record remains separate")
    return checks


def expect_detection(description: str, d3d9: str = D3D9, diagnostics: str = DIAGNOSTICS, header: str = HEADER) -> None:
    try:
        validate(d3d9, diagnostics, header)
    except AssertionError:
        return
    raise AssertionError(f"negative control was not detected: {description}")


count = validate(D3D9, DIAGNOSTICS, HEADER)
expect_detection("disabled fast path removed", d3d9=D3D9.replace("if (!DetailedDiagnosticsEnabled()) return lock();", "", 1))
expect_detection("buffer accidentally included", d3d9=D3D9.replace("ShadowLock(b, 0, p, size, flags)",
                                                                  "ShadowLock(b, 0, p, size, flags, true, true)", 1))
expect_detection("final rect record removed",
                 diagnostics=DIAGNOSTICS.replace('Log("diag: rect locks %s", TakeLockTimingStats().c_str());', "", 1))
expect_detection("non-strict threshold", header=HEADER.replace("if (ns > kThresholds[i])", "if (ns >= kThresholds[i])"))
print(f"Rect-lock diagnostic structural checks passed ({count}; 4 negative controls detected)")
