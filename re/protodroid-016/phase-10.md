# Phase 10 — Vulkan GPU paths, synchronization, memory, and presentation

Status: **active**. Started **2026-11-04T13:03:30.762524Z** from Phase 9 completion `e751b524c8fa6a41a69795502b6076eca670f90c`. Phase 9 is complete (finalizer 41/41; manifest 592 checks); Phase 11 remains blocked until Phase 10 completes.

## Exact source and settings

- ProtoDroid production baseline is the Phase 9 validated source at `39ff0f2c3b8bccc303e2d53e204df8abbc390d81`; `e751b52` is documentation-only after it.
- Pinned renderer: DXVK v2.5.3 `c707d9026f33b6ab89639f154b6ac5f6326fa037` plus all 16 ordered `android/patches/dxvk-*.patch` files. The independently patched Phase 9 clone and source hashes are reused read-only as the baseline.
- Historical diagnostic-tour source is `51e51ea3b20927e40e94135a6a09633c616dcf61`, not the current candidate. Its Mali-G720 MC7 49.1.0 run negotiated Vulkan 1.3, `d3d9.presentInterval = 1`, dynamic FIFO, six 2712x1220 swapchain images, anisotropy 4, and Prototype DF formats disabled. It contains no `present:` breakdown because `DXVK_PRESENT_STATS` was not enabled.
- Ordinary Play keeps detailed pipeline/present diagnostics opt-in. The diagnostic tour remains isolated; no normal cache/save migration or visual-pass claim is inferred.

## Non-negotiable invariants

Preserve real engine-presented frames; dynamic FIFO and Reset stability; Android surface-destroy locking; D3D9 device-lock/unlocked-wait ownership; Mali compile/present workarounds; exact Vulkan capabilities and driver selection; suboptimal-rotation handling; frame-latency semantics; command submission and WSI semaphore/fence lifetimes; unified-memory fallbacks; dynamic shadows, Bink, Classic controls, AA/texture quality, mods, ARM64, and safe defaults. No mailbox/immediate switch, frame skip, feature spoofing, resolution/effect reduction, speculative queue rewrite, or generated-frame metric is allowed.

## Source-led hypotheses

### H1 — normal Play pays for disabled Present diagnostics

`src/dxvk/dxvk_presenter.cpp` in `dxvk-android.patch` calls `nowUs()` around queue pre-acquire and updates `g_preFenceUs`/`g_preAcqUs` on every presented frame regardless of `DXVK_PRESENT_STATS`. The application-side acquire fallback similarly clocks and updates three counters. In contrast, `D3D9SwapChainEx::PresentImage` gates its own clocks/counters on a lazy `statsOn` read after the host sets the environment.

Candidate: propagate that one-time option state through a relaxed atomic flag and retain a non-instrumented Vulkan path when disabled. The wait, acquire, queue-present, frame-index, semaphore, fence, and result operations must remain identical; enabled mode must retain the original measurements and 300-frame report. Benchmark only the exact diagnostic scaffolding on host/native ARM64; this is not an Android FPS measurement.

**Candidate checkpoint, not adopted:** the exact full-patch source passes function extraction, lazy-publication identity, success/suboptimal/error paths, call order, semaphore/fence state, feature-chain, and enabled/disabled counter tests. ASan+UBSan and TSan are clean. All 16 patches apply, the minimal patch produces the same tree as the independently generated candidate, and existing memory/depth/FXAA/preflight/BC tests pass. Full-source host timing (`present-host-full-final/result.json`, 24 rounds × 1,000,000 calls plus 24 enabled rounds) reports disabled queue pre-acquire baseline/candidate medians **106.172747/9.096117 ns**, paired saving **97.064736 ns**, 24/24 faster; application fallback saves **103.675612 ns**. A/A paired ratio is **0.993155**, and enabled candidate/baseline is **1.008219** (sub-nanosecond paired median regression; opt-in diagnostics). This is only about **0.000097 ms per normal Present** on this host, so a favorable ratio is not sufficient for adoption. Native ARM64 and absolute-materiality review remain required. Failed harness attempts preserve the stub compile fix, corrected pNext expectation, and removed zero-delta assumption; no production failure was hidden.

### H2 — presentation policy is not supported as an optimization target by the historical data

The native tour has 60 detailed windows and 61 Present-wall records. Present wall includes FIFO/vsync and is not GPU time. Parse the disjoint windows, correlate interval and Present-wall threshold counts, and inspect the largest outliers before considering any synchronization change. Absent DXVK `present:` submit/acquire/latency decomposition, do not blame the swapchain for long engine/render intervals or change FIFO, image count, frame latency, or lock ownership.

### H3 — memory and queue paths require compatibility-first audit

Audit the patched memory-type candidate masks, managed-texture system-copy behavior, resource waits, submission status, swapchain recreation idle waits, and present fences. Existing Qualcomm/Mali compatibility paths are correctness requirements. A change needs an exact affected workload and correctness evidence; general UMA, barrier removal, queue batching, or allocation rewrite is not justified by the cache/pipeline trace.

## Evidence plan

1. Freeze original patch/source/log hashes and parse the historical Present records with raw output.
2. Rebuild the candidate patch from pinned upstream; compare every patched file and prove only the intended Present diagnostics change.
3. Exercise enabled/disabled instrumentation semantics and exact Vulkan-call order in a host fixture; use sanitizers and source assertions for the unmodified synchronization operations.
4. Measure interleaved A/A and before/after diagnostic overhead on host and native Linux ARM64, preserving raw per-round results and paired statistics. Reject if the disabled path is not consistently better or enabled diagnostics regress materially.
5. Run all patch applications, DXVK policy fixtures, broad regression, ASan+UBSan/TSan where applicable, exact-code ARM64 CI, and real APK compilation. No device FPS/pacing claim without a physical-device A/B.
6. Record adoption/rejection and remaining GPU/memory/synchronization leads before the seven-part completion report and post-commit finalizer/manifest.
