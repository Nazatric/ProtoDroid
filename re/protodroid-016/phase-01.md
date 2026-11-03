# Phase 1 evidence checkpoint — baseline and measurement laboratory

Status: **available-environment baseline established; physical-device baseline still open**. This is not a claim of full device-baseline completion.

## 1. Actual time and work

- Started: `2026-11-03T15:50:07+00:00`
- Available-environment capture checkpoint: `2026-11-03T15:58:09+00:00`
- Active elapsed time at checkpoint: **8 minutes 2 seconds**; no idle-time or elapsed-time padding is claimed.
- Verified the exact clean 0.15 starting point, inventoried real tools, established the controlled protocol/scene matrix, ran repeated host and production-Bink baselines, recovered exact-0.15 ARM64 CI evidence, validated measurement tests, and audited supplied log provenance.

## 2. Files and inspected revisions

- Baseline/release: `18ba66bd0945e405aaded0de5ff8848a8cd07932`, `protodroid-v0.15`.
- Added the persistent records in this directory and `tools/perf_evidence.py` plus its tests; added the test to CI.
- Inspected existing Present/timing/diagnostic/tour instrumentation and host fixtures at the baseline.
- Read-only evidence: root `51e51ea3…` logs and `TestingFiles` `b2ca160b9f59edb15fe8be40d040f4662985f99e`.

## 3. Technique decisions

- Adopted: reproducible host references and strict evidence comparability guards. No runtime optimization was adopted.
- Rejected: combining different source revisions into one run; relabeling historical tours or host/CI timings as physical 0.15 FPS.
- Inconclusive/open: every physical-device FPS, pacing, shader, loading, thermal, and sustained-play baseline.

## 4. Raw measurements

- Five-run mixer reference: median 4.830 µs/call, range 4.798–4.927.
- Five-run DXT decode medians and ranges: DXT1/BGRA 288.430 (284.909–291.463) µs; DXT1/A1R5G5B5 295.390 (294.473–302.481) µs; DXT3 620.433 (617.163–653.695) µs; DXT5 732.531 (721.278–748.096) µs.
- Five-run indexed input reference: 0.238 µs/poll, range 0.236–0.240.
- Ten-run real Bink/RCF cold-index reference: median 14.8776 ms, range 14.5426–16.4952.
- Exact-0.15 ARM64 CI values and two-pass DXT references are recorded in `baseline.md`.
- Raw evidence and hashes: `/tmp/protodroid-016/baseline`. No before/after optimization delta exists yet.

## 5. Correctness

- Mixer 0/260, biquad 0/300, ADPCM 0/200, and skin 0/2000 differences on every baseline run; stable benchmark digest.
- Stable DXT digests and input checksum.
- All real Bink/RCF/RZ/memory/custom-I/O, decode/copy/wrap/audio checks passed at both rates.
- Graphics, input, Present, diagnostics, guest-time, runtime-bridge, and real-engine UBSan checks passed.
- Evidence-parser synthetic/real-log tests and comparability guards passed.

## 6. Limitations

No Android device, `adb`, SimplePerf, Perfetto, GPU profiler, or local Android build toolchain is available. Host and ARM64-runner measurements are not phone FPS, frame-time, rendering, loading, latency, or thermal evidence. Historical logs are from other source revisions and the tour uses a 4x guest clock. The physical 0.15 baseline remains mandatory when hardware is available.

## 7. Next priority

Phase 2: inspect all five required external repositories at recorded commits and licenses, trace techniques to implementations, and populate the evidence-ranked matrix. Phase 3 must resume the physical profiling gate rather than treating this host baseline as a substitute.
