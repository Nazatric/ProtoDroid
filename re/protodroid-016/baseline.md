# Phase 1 — reproducible 0.15 baseline

## Identity and integrity

- Release: ProtoDroid 0.15, `protodroid-v0.15`
- Baseline source (and local HEAD at phase start): `18ba66bd0945e405aaded0de5ff8848a8cd07932`
- Release APK: 59,023,246 bytes, package `com.protodroid`, versionCode 15, SHA-256 `ff0f3ec6b5f979ac5cc4ba47a8bd61150ca2b1aa300243b9d14a7257a197c8c2`
- Original engine SHA-256: `d331be205bdde99897952ee88261ab7e63c5c65e36a4660e2a367886a3537071`
- Working tree was clean at `2026-11-03T15:50:07+00:00`.

## Measurement capability actually available

Current sandbox: x86_64 Linux, two logical CPUs, 3.8 GiB RAM. GCC/GNU binutils and Python are available. There is no `adb`, attached Android device, SimplePerf, Perfetto, Linux `perf`, Ghidra, official REA, LeanREA, Android SDK/JDK, RenderDoc, or `glslangValidator` in this environment. Therefore:

- Current physical-device FPS, GPU time, input latency, thermal, and shader-stall measurements are **unavailable**.
- Host timing is a microbenchmark only and will never be reported as phone FPS.
- Exact ARM64 code generation and Android packaging require CI; an ARM64 runner is still not a physical phone GPU.

## Existing evidence boundary

The repository contains a detailed Mali-G720 diagnostic-tour log, but it identifies source `51e51ea3b20927e40e94135a6a09633c616dcf61`, not released 0.15. It records a 2412DPC0AI/MT6899 device, Android 16, Vulkan 1.3.278, 120-Hz display, 2400x1080 render size, Game-default AA (`game_aa_level=-1`), shadow detail 2, graphic quality 2, and FIFO interval 1. It is useful for instrumentation and historical bottleneck research only. It is not a controlled 0.15 performance baseline, and its 4x guest-clock tour is not normal-play FPS evidence.

Read-only `TestingFiles` commit `b2ca160b9f59edb15fe8be40d040f4662985f99e` was also checked. Its `proto-native.log` identifies `54ef821e238e504afcac9fb4200018d5a619f3e9`, while its `journal.log` identifies `e34ebb066ffe70717eb79259ad6d6421f4987193`. They are separate historical runs, not one evidence set. The former has no Present samples in the supplied file; the latter has 20 tested phases with timing fields but no matching device metadata in that journal. `tools/perf_evidence.py` rejects their combination. No performance or issue-resolution conclusion is drawn from them.

## Required device benchmark protocol

For every A/B comparison, record the exact APK source/hash and certificate, device build, SoC/GPU, driver/version, Android version, refresh rate, thermal state, battery/charging state, and all game/mod/driver settings. Keep these constant between baseline and candidate. Use the same save and starting position. Do not silently clear user data, shader caches, or saves.

Run baseline and candidate in interleaved order where practical (A/B/B/A, then repeat), with a cooldown and identical warm-up policy. Retain every run rather than selecting the best. Report raw frame intervals when available plus median/mean FPS, frame-time p50/p95/p99/max, counts over 8.5/17/34/50/100/250/1000 ms, and explicit cache state. Average FPS alone is not an adoption gate. At least five paired runs are preferred when the scene and hardware permit; fewer runs are labeled preliminary.

### Scene matrix

| Scene | Required conditions | Primary measurements |
| --- | --- | --- |
| Cold startup to first gameplay | Fresh isolated app cache; exact cache-clearing step recorded | Startup stages, pipeline/cache activity, long stalls |
| Warm startup to first gameplay | Second launch with intact isolated cache | Startup stages and cache-hit behavior |
| First shader-heavy gameplay encounter | Same isolated save/path; no prewarm | First-use compilation and frame-time spikes |
| Static outdoor view | Same position/camera; 60 s after warm-up | CPU/GPU baseline and pacing |
| Dense outdoor streaming | Same recorded movement route | I/O, allocation, upload, and long-frame behavior |
| Combat/particles | Same encounter and inputs as practical | CPU/GPU peaks, p95/p99, audio continuity |
| Building/wall traversal | Same route and movement inputs | Streaming, skinning, shadows, and pacing |
| Bink/menu playback | Same movie/menu and audio rate | Decode/copy cost, A/V sync, menu-FPS behavior |
| Sustained ordinary play | At least 30 min where hardware allows | Thermal, memory growth, throttling, sustained p95/p99 |
| Isolated 41-phase diagnostic tour | Tour-only 4x guest clock and isolated saves/cache | Repeatable regression phases, not normal-play FPS |

## Existing host fixtures

- `src/native/tests/run_hot_test.sh`: real-engine mixer/biquad/ADPCM/skin differentials plus informational mixer timing.
- `src/native/tests/dxt_benchmark.cpp`: deterministic DXT1/3/5 decode throughput and output digest.
- `src/native/tests/input_lookup_benchmark.cpp`: linear-versus-indexed keyboard lookup with differential checks.
- Production Bink harness: real decoder, loose/RCF/RZ/memory/custom-I/O coverage at both audio rates.
- Native UBSan suites and exact ARM64 CI: correctness gates, not physical performance proof.

## Captured 0.15 host references

Five fresh runs used the original engine above and production sources at `18ba66b`. Values below summarize per-run medians; ranges show run-to-run variation, not phone frame times.

| Fixture | 0.15 result | Output/correctness |
| --- | --- | --- |
| Mixer | 4.830 µs/call; range 4.798–4.927 across five runs | Stable digest `1b1304bebe7b5bc3`; mixer 0/260, biquad 0/300, ADPCM 0/200, skin 0/2000 differences in every run |
| DXT1 → BGRA | 288.430 µs/decode; range 284.909–291.463 | Stable digest `389410` |
| DXT1 → A1R5G5B5 | 295.390 µs/decode; range 294.473–302.481 | Stable digest `781134` |
| DXT3 regression control | 620.433 µs/decode; range 617.163–653.695 | Stable digest `1139944` |
| DXT5 regression control | 732.531 µs/decode; range 721.278–748.096 | Stable digest `1497235` |
| Indexed input lookup | 0.238 µs/poll; range 0.236–0.240 | Stable checksum `1320000`; linear control median 5.881 µs/poll |
| Real Bink/RCF harness | 14.8776 ms median cold RCF index; range 14.5426–16.4952 over ten runs | All loose/RCF/RZ/memory/custom-I/O, decode, plane/copy, wrap, and audio checks passed at 32000 and 44100 Hz |

DXT used 400 iterations and input used 20,000 polls per run. Bink used the external 15,563,652-byte `valve.bik` fixture, SHA-256 `e4cc59c1242615fc33159537e21d019599481676f1de62818a5cc4aa9d2f2dd5`; the fixture is not imported into Git. Host RCF time includes host storage and is not Android startup time.

The graphics, input, Present, diagnostics, guest-time, runtime-bridge, and real-engine UBSan baselines all passed in the historical Phase 1 runs. Raw logs, commands/hashes, and `summary.json` were outside Git in `/tmp/protodroid-016/baseline`; that original directory was lost during sandbox recreation. These remain recorded historical results, not claims that the original raw files are still available or have been regenerated.

## Existing exact-0.15 ARM64 CI reference

Release run `41660305950`, job `126001422675`, checked out `18ba66b` on `ubuntu-24.04-arm` image `20261025.165`. The current-source DXT reference (two passes, 200 iterations) has median decode times of 181.625 µs (DXT1/BGRA), 185.9895 µs (DXT1/A1R5G5B5), 320.344 µs (DXT3), and 411.5095 µs (DXT5), with stable digests. One informational run reports mixer 4.083 µs/call and indexed input 0.183 µs/poll. These are single/two-run CI microbenchmarks, not sustained or physical-device results. The authorized signed-log excerpt is retained as `arm64-ci-excerpt.txt`; artifact IDs and server-reported zip hashes are recorded there. Ordinary downloads returned blob EOF, so those server hashes are not claimed as locally recomputed.

## Evidence tooling

`tools/perf_evidence.py` extracts source/device/settings, raw Present windows, and tested-phase metrics from native logs. Its comparator marks device, GPU, configuration, phase, or metric coverage mismatches as non-comparable. `tools/test_perf_evidence.py` covers synthetic A/B data, mismatch guards, and the supplied historical logs. This tooling does not infer FPS where no device measurement exists.

For exact global frame-time A/B evidence, first run `tools/analyze_frame_intervals.py` on each closed raw CSV and `tools/perf_evidence.py summarize` on each matching native-log set. Then use:

```sh
python3 tools/perf_evidence.py compare-raw \
  baseline-summary.json candidate-summary.json baseline-frames.json candidate-frames.json \
  --baseline-scene static-outdoor --candidate-scene static-outdoor \
  --baseline-cache-state warm --candidate-cache-state warm
```

The raw comparator requires source/device/GPU/configuration metadata, matching caller-declared scene and cache-state labels, and closed exact reports. It compares global nearest-rank p50/p95/p99/max, average interval/rate, and threshold counts normalized per 1,000 completed intervals when row counts differ. Missing metadata, mismatched protocol, or an unclosed prefix is non-comparable. The labels are protocol declarations, not independent scene/cache proof; a single comparison is not significance, visual validation, or automatic adoption.

Physical-device 0.15 baseline capture remains an explicit open gate for Phases 3, 10, and 12; no current phone FPS number is available to copy or estimate.
