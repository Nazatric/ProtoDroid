# Phase 3 completion report — whole-system profiling and bottleneck map

Status: **complete for the available source/host profiling**. Active work exceeded one hour. Physical-device total-frame attribution remains unmeasured and is not represented as satisfied. No production optimization or device FPS result is claimed.

## 1. Actual time and work

- Started: `2026-11-03T18:21:53Z`, after Phase 2 completion commit `a5c9c22e97fccc6e44c5cceb50dbb4a462e234bc`.
- Inventoried genuinely available profiling tools. GNU gprof 2.40 is available; strace, perf, Valgrind, SimplePerf, adb, and glslangValidator are unavailable.
- Traced existing Present, diagnostic, guest/import, and DXVK pipeline/synchronization instrumentation entry points.
- Built and ran matched normal/`-pg` production DXT and native mixer workloads, retained raw gprof output, and checked output/correctness before interpreting attribution.
- Restored the real Bink/RCF stack; measured process-cold index versus warm archive paths and corrected full-sample decode/registered-plane cohorts; analyzed historical pipeline/Present instrumentation and validated current Present/diagnostic/evidence-parser tests.
- Added digest-checked DXT size and order-balanced destination-working-set sweeps, then inspected the distinct x86/ARM64 store paths instead of assuming a cache cause.
- Generated and analyzed targeted AArch64 assembly for DXT, native/generated mixer functions, and dispatch with an isolated LLVM toolchain after preserving unavailable-package/target/header failures.
- Completion clock reading: `2026-11-03T19:26:15Z`; cumulative active phase time **1 hour 4 minutes 22 seconds** (3,862 seconds). No idle-time or elapsed-time padding.

## 2. Files and inspected revisions

- Profiling source: `a5c9c22e97fccc6e44c5cceb50dbb4a462e234bc`; no runtime file changed and no ProtoDroid commit was created during active profiling. The completion commit contains research records only.
- Updated research records: `phase-03.md`, `profiling.md`, `experiment-ledger.md`, `technique-matrix.md`, `rejected-changes.md`, `README.md`, `final-report.md`, and `device-results.md`.
- Production paths: `src/native/d3d9_dxt.cpp`, `hot_audio.cpp`, `hot_skin.cpp`, `bink_media.cpp`, `bink.cpp`, `bink_blit.cpp`, `path.cpp`; existing DXT/hot/Bink harnesses; unchanged `prototypeenginef.dll` with its recorded hash. RCF evidence uses retained test-only profile harnesses/diffs outside Git.
- Instrumentation inspected: `present_stats.*`, `d3d9.cpp`, `diagnostics.cpp`, `diagnostic_tour.cpp`, and `dxvk-zzzzzzzzz-pipeline-timing.patch`/Android DXVK timing output.
- Persistent raw evidence: `/home/user/protodroid-016-evidence/profiling/`, including exact commands, normal/pg logs, `gmon.out`, flat/call graphs, source/binary hashes, preserved setup failures, test-only harness diffs, raw JSON, and targeted assembly. The final comprehensive `artifact-hashes.txt` covers **1,281 files / 1,406,612,537 bytes**, including raw gmon/work/object files; it was regenerated and verified without self-hashing the manifest outputs. The earlier 35-file lists are retained separately.
- A mixer test-only copy changes the guest-header include form and adds an early 50-repeat benchmark-only branch. Production `BenchmarkMixer` and all production native functions are unchanged. The full unmodified differential harness runs separately.

## 3. Technique decisions

- **Adopted:** none; these are baseline profiling experiments only.
- **Profile-guided candidates, not optimizations yet:** indirect-dispatch once/hash and generated TSO costs; DXT color-block/destination-store paths; RCF index/normalize/path lookups; Bink registered-plane copies; native mixer self/inlined code. Any later change requires a separate hypothesis, matched A/B, differential correctness, and ARM64/device evidence.
- **Rejected as evidence:** treating gprof self percentages or invalid call arcs as phone/frame attribution; comparing different DXT protocols as A/B; treating mixer repetition differences as a 0.16 gain; using the stack-corrupted Bink cohort; deriving clean pipeline timing from heavy historical tracing; or converting assembly instruction counts into cycles/FPS.
- **Inconclusive:** actual game DXT/mixer share, Present/pacing impact, driver wait, GPU time, shader stalls, loading, thermals, and sustained device performance.

## 4. Raw measurements

### DXT production decoder, 2,000 iterations

Normal versus `-pg` median decode times:

| Case | Normal µs | `-pg` µs | Instrumentation ratio | Matching digest |
| --- | ---: | ---: | ---: | --- |
| DXT1 → BGRA | 256.806 | 266.289 | 1.0369 | 1947643 |
| DXT1 → A1R5G5B5 | 266.629 | 275.575 | 1.0336 | 3898345 |
| DXT3 → BGRA | 548.775 | 740.528 | 1.3494 | 5683106 |
| DXT5 → BGRA | 630.633 | 809.197 | 1.2832 | 7451392 |

Gprof flat profile: `DecodeDxt` 58.05% self time; `DecodeColorBlock` 41.22% self time over 459,407,360 calls. Inlined alpha/other helper work is not separately attributed. The instrumentation ratios are overhead measurements, not candidate speedups.

A separate seven-batch normal size sweep used 64/256/512/1024/2048-square images with 20,000/4,000/2,000/250/64 iterations respectively. Median µs/call:

| Size | DXT1→BGRA | DXT1→16-bit | DXT3 | DXT5 |
| --- | ---: | ---: | ---: | ---: |
| 64 | 3.113 | 2.961 | 8.058 | 8.476 |
| 256 | 58.191 | 61.647 | 133.512 | 148.375 |
| 512 | 258.653 | 261.281 | 540.566 | 630.217 |
| 1024 | 1,068.621 | 1,087.312 | 2,224.180 | 2,549.066 |
| 2048 | 4,274.575 | 4,346.024 | 8,898.198 | 10,072.598 |

All four 512/2,000 cumulative digests exactly match the original benchmark; production graphics UBSan passes. The repeated 512 medians differ from the earlier normal run by **+0.72%/-2.01%/-1.50%/-0.07%**, a same-binary variability observation, not a candidate delta. Per-pixel cost rises from small working sets and plateaus around 1024/2048; cache/memory influence is only a hypothesis without counters. Different size/iteration digests are not interchangeable. Raw logs, JSON, ns/pixel calculations, harness diff, and hashes are in `profiling/dxt-gprof/dxt-size-sweep-*`.

A destination-only rotation then held the 512 source, mutations, and decoded bytes constant while varying 1/4/16/32 destination slots. Six repetitions (three ascending, three descending) produced 24 processes and 96 case results; every digest matched the original. Cells are median µs/call and median within-repetition ratio to that repetition's one-slot baseline:

| Slots | DXT1→BGRA | DXT1→16-bit | DXT3 | DXT5 |
| --- | ---: | ---: | ---: | ---: |
| 1 | 260.346 (1.000×) | 264.839 (1.000×) | 554.143 (1.000×) | 640.221 (1.000×) |
| 4 | 261.839 (1.010×) | 265.402 (0.999×) | 620.890 (1.120×) | 695.671 (1.085×) |
| 16 | 263.098 (1.014×) | 267.291 (1.009×) | 620.726 (1.119×) | 699.877 (1.102×) |
| 32 | 264.525 (1.007×) | 269.015 (1.018×) | 629.148 (1.161×) | 756.924 (1.185×) |

DXT3 paired-ratio ranges are 1.082–1.150 (4), 1.085–1.158 (16), and 1.129–1.388 (32); DXT5 ranges are 1.058–1.113, 1.069–1.107, and 1.041–1.292. Effects persist across both slot orders, but outliers/frequency drift remain and no cache counters exist. Source inspection shows specialized direct row stores for x86 DXT1 versus generic temporary-tile/per-pixel stores for x86 DXT3/DXT5; ARM64 DXT3/DXT5 instead uses NEON full-tile stores. This supports host destination-working-set/store-path sensitivity, not a proven cache mechanism or ARM64/device conclusion. An initial harness-transformation assertion failed before any run and is preserved.

### Native mixer, unchanged 2,000-call benchmark repeated 50 times

- Full unmodified harness's first post-differential benchmark: 4.649 µs/call.
- Normal benchmark-only 50-run median of medians: 3.642 µs/call; `-pg`: 3.954 µs/call; instrumentation ratio 1.0857.
- All full/normal/pg benchmark outputs share digest `1b1304bebe7b5bc3`.
- Gprof attributes 100.02% of sampled self time (2.80 seconds, sampling/rounding) to `Native_105e8860`, 706,400 calls; internal helpers are inlined and not separately ranked.
- The first-post-differential versus repeated median difference demonstrates run/repetition/cache-state sensitivity. It is not a before/after optimization comparison and is not compared with Phase 1's historical 4.830 µs/call as a gain.

These are x86_64 host microprofiles, not Android FPS, frame-time, audio-latency, or GPU results.

### Targeted AArch64 generated-code inspection

The release APK could not be downloaded because GitHub redirected to the sandbox-blocked `release-assets.githubusercontent.com`; no package/binary was inspected. No local NDK, Clang cross compiler, or QEMU was installed. An isolated **ziglang 0.17.0 / LLVM 22.1.8** toolchain instead generated assembly from pinned source. Android-target attempts failed on CPU-option parsing and then missing Bionic headers; successful output uses `aarch64-linux-musl`, generic ARMv8.2+RCpc, Cortex-A76 tuning, production-like `-O2`, flat-memory and TSO flags. Zig also appends outlined-atomics mode. This is a targeted instruction model, not Android/Bionic or release-binary equivalence, and was not executed or timed.

- DXT exported function: 1,209 parsed instructions; whole-file output includes 12 `LD1` lane forms and four vector `STR Q` stores (zero `ST1`). This confirms NEON full-tile forms in the generated target code.
- Native mixer `Native_105e8860`: 251 instructions, zero `FMLA`/`FMADD`; generated reference `f_7c5e8860`: 1,368 instructions, 50 `LDAPR`, eight `STLR`, zero `FMLA`/`FMADD`. Counts describe structure, not cycles or threading correctness.
- `RecompLookup`: 43 instructions with an acquire once-flag fast path before multiply/shift hash probing. `RecompDispatch` has two inlined lookup/once paths and two static import-dispatch call sites; its 221 instructions and `DispatchImport`'s 493 include cold/error/profiling paths and are not hot-path costs.

These observations motivate Phase 4 measurement of indirect-dispatch once/hash and TSO overhead with exact differential tests; no assembly-only optimization is adopted. Initial target/analysis failures—including an invalid unconditional shell verification message—are preserved. Assembly, extraction JSON/text, commands, and hashes are under `profiling/arm64-assembly/`.

### Historical instrumentation analysis — not a 0.15/0.16 A/B

A read-only parser now verifies and analyzes the revision-`51e51ea3b20927e40e94135a6a09633c616dcf61` diagnostic-tour file set. Native/journal source IDs match; DXVK/trace identity limitations are explicit. The 171 graphics timing calls, 171 requested pipelines, zero errors, exact trace sequence IDs 0–170, and 342/342 declared stages agree. Sequence 6 has two zero-byte/zero-hash unavailable-SPIR-V records, not proof of malformed shaders. Excluding them gives 80 unique vertex hashes, 87 fragment hashes, 97 unique known VS/FS pairs across 170 creations, 73 repeated-pair instances, and maximum pair multiplicity six; repeated pairs can have different pipeline state.

- Approximate reset-counter aggregates: graphics compile-mutex wait 4,613.235 ms total / 26.977981 ms weighted mean / 204.658 ms max; driver calls 8,644.179 ms / 50.550753 ms / 154.41 ms max. Eight compute calls have approximately 57.558 ms total driver time. These are compiler/serialization measurements, not equivalent render-thread stalls.
- `wait_*` is compile-mutex acquisition; `driver_*` is `vkCreate*Pipelines` duration. Relaxed counters reset on emission, and emission is triggered by active compilation rather than fixed frame-aligned windows. All 171 calls had heavy shader dumps enabled inside the compile lock, so driver/wait/frame timings may include trace/fsync overhead. Future device timing must use lightweight timing with `pipeline_trace=0`; heavy structural tracing is a separate run mode.
- The historical 1,032.764 ms worst-Present window has 1,036.180 ms EngineService max, only 1.129 ms Present-wall max, and zero reported DXT/I/O work. It does not establish a cause; Present wall is not GPU time.
- Separate early rolling windows report up to 99.13 ms DXT work (2,174 decodes, 6.42 ms max) and 99.9 ms read I/O (50 reads, 7.37 ms max). These non-independent windows are not summed or treated as normal-play FPS evidence.

Provisional investigation order, not a measured device total-frame ranking: (1) resolve first-use pipeline/loading stall attribution on a current device; (2) preserve early streaming DXT/RCF I/O coverage; (3) measure sustained EngineService/Present pacing and thread activity; (4) measure Bink decode/registered copies only in movie scenes; (5) keep mixer below those until device audio/CPU evidence justifies otherwise. Controlled host workloads have no common frame denominator, so their values are never ranked numerically across harnesses. No technique is adopted or rejected by this historical analysis.

Raw parser, JSON, exact excerpts, and source hashes are under `profiling/historical-instrumentation/` in the persistent evidence store.

### Measurement-tool validation

UBSan builds pass `present_stats_test` (baseline, one ADPF report per Present, FPS/percentiles, detailed totals), `diagnostics_test` (opt-in default, timing windows, CPU availability, worst events, `/proc` parsing), and `perf_hint_test`. `tools/test_perf_evidence.py` passes its fixtures, real-log coverage, mixed-revision rejection, and comparability guards.

The repository parser summarizes historical source `51e51…` with 61 Present-wall records and 20 tested phases. A labeled self-compare is correctly `comparable=false`: the old journal lacks common per-phase performance metrics. This is a guard validation, not an A/B. Its `present_windows` JSON field parses **Present API wall duration**, not `diag: window` Presented-frame intervals; current per-phase frame metrics are separate. An initial summary assertion incorrectly assumed 60 Present-wall records; the actual 61 and failure are preserved.

Raw logs, JSON, commands, and hashes are under `profiling/instrumentation-validation/`. These passes validate instrumentation logic only, not device FPS or timing accuracy on hardware.

### Current RCF/Bink host profiles

The real CI-style Bink stack was rebuilt outside Git: FFmpeg n7.1.1 `db69d06eeeab4f46da15030a80d539efb4503ca8`, zlib v1.3.1 `51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf`, and the 15,563,652-byte `valve.bik` sample at repository `48bd8df605e36f4c66d365acb9843ac77bb76cf5`, SHA-256 `e4cc59c1242615fc33159537e21d019599481676f1de62818a5cc4aa9d2f2dd5`. The unmodified UBSan Bink suite passes at 32,000 and 44,100 Hz.

The table reports median (p95), with 50 fresh index-cold processes and 10×20,000 warm open/48-byte-read/close operations per build. Cold is process/index-cold, not dropped OS/storage cache. Full and focused harnesses are not timing-comparable because layout, fixture reuse, and path implementation differ.

| Harness/cohort | Normal | `-pg` | Overhead ratio of medians |
| --- | ---: | ---: | ---: |
| Full, cold index ms | 13.340700 (14.148600) | 14.441200 (15.172000) | 1.082492 |
| Full, warm µs/lookup | 17.627550 (18.011500) | 17.747850 (19.790700) | 1.006825 |
| Production `path.cpp` + `bink_media.cpp`, cold ms | 18.536700 (20.400800) | 19.445050 (21.458900) | 1.049003 |
| Production `path.cpp` + `bink_media.cpp`, warm µs/lookup | 12.519750 (12.863000) | 12.913600 (13.312400) | 1.031458 |

All normal/pg and full/focused data digests match: cold `852fa1ad3927b57d`, 20,000-iteration warm `4c6fe5c0653cf6bd`. Deep attribution adds 500 index-cold `-pg` processes and 100×20,000 production-path warm `-pg` lookups, all digest-matched.

Gprof call arcs are invalid for these optimized RCF harnesses: they attribute unreachable `CheckPublic`/`MemorySource::Read` calls and hundreds of calls to one actual `BuildIndex`, even non-PIE. Only reachable flat PC samples are considered, and startup/teardown/harness work is excluded from interpretation:

- 194-sample cold flat profile: 41.24% archive hashtable `_M_find_before_node`, 14.95% Bink `Normalize`, 9.28%/3.61% sort helpers, 2.06% `IndexArchive`; `_init` 14.43% and global teardown 13.92% are outside the timed lookup.
- 202-sample production-path warm flat profile: 16.98% Bink `Normalize`, 13.84% production `HostPath`, 11.95% hashtable find, 5.03% `OpenFileSource`, 3.77% production path `Normalize`, 3.14% `FileSource::Read`, 1.26% `GuestCurrentDir`. Global teardown 10.06%, `_init` 8.18%, and harness `ReadMix` 8.18% are excluded. Percentages are raw total-process PC samples, not wall-time fractions; syscalls/uninstrumented libraries are outside the histogram.

These are candidates for later controlled memory/I/O investigation, not adopted optimizations. The standalone harness's different `HostPath` stub was explicitly rejected as production attribution. Missing-zlib/include-path setup failures and invalid PIE/non-PIE call graphs are preserved. Exact commands, source/binary/fixture hashes, raw logs, gmon files, and limitations are under `profiling/rcf-bink-gprof/`.

### Corrected full-sample Bink decode profiles

Two distinct normal `-O2` cohorts each ran 20 fresh processes × 291 sequential 1920×1080 frames. Only per-frame `BinkDoFrame` is summed; YV12 export/hash, input checks, and plane comparisons are outside that timing. Values are medians across per-run summaries, not pooled frame percentiles:

| Memory-backed path | Open ms | Decode total ms | Mean ms/frame | Median ms/frame | Per-run p95 ms | Per-run p99 ms | Max ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Unregistered | 1.447354 | 545.069728 | 1.873092 | 1.117999 | 4.490041 | 5.052517 | 5.452108 |
| Two registered plane sets | 1.403390 | 677.875458 | 2.329469 | 1.576090 | 4.952626 | 5.538735 | 6.006153 |

Both pass 20/20 processes with common decoded-YV12 digest `d68445b7a59b9b4a` and unchanged input after every frame. The registered path also matches both padded negative-pitch plane sets to exported YV12 after every frame; its `BinkDoFrame` includes production uploads to both sets. The cohort difference is descriptive, not pure upload cost or an optimization A/B.

An earlier 20-run unregistered cohort is **invalid**: its evidence-only guest `Call()` stack `0x400000` overlapped the large input starting at `0x100000`. A later check proved a frame-1 input change at offset 3,145,712 (`0x2ffff0`); the earlier registered frame-73 invalid-data failure is therefore harness corruption, not a production decoder result. Old digest `eaf346552382e543` and timings are excluded. Raw failures and the original summary remain, while `decode-summary.txt` is marked invalid. Corrected harnesses allocate a dedicated 64-KiB stack first and verify input after every frame.

These are offline host decode/demux/audio-packet and registered-copy profiles, not storage, rendering, audio-device output, real-time playback, phone, or gameplay FPS evidence. FFmpeg has no x86asm; rates do not transfer to Android.

## 5. Correctness

- All four DXT normal/pg digest pairs match exactly. The size sweep's four 512/2,000 digests also match, all 20 size/format rows satisfy the count/protocol assertions, and production graphics UBSan passes. All 96 destination-slot case results also match the required original digests. Digests across different sizes/iteration counts are separate baselines, not expected matches.
- Unmodified production Bink UBSan suites pass at 32,000/44,100 Hz. All 100 full and 100 focused index-cold timing runs, 400,000 full and 400,000 production-path timed warm lookups, and deep 500-cold/2,000,000-warm attribution cohorts match their required data digests. No profile used a dummy sample or fallback decoder.
- Corrected Bink decode cohorts pass 20/20 processes per path, 11,640 total frames, with common digest `d68445b7a59b9b4a`, per-frame input integrity, and per-frame both-set registered-plane equality. The prior stack-overlapping cohort and its digest/timings are explicitly invalid; its failures are preserved, not hidden.
- Historical parser assertions verify matching native/journal source IDs, four timing summaries, exact graphics trace sequence IDs 0–170, 171/171/0 call/request/error totals, and the worst-window values. Source/log hashes are retained; no source log was edited.
- The unmodified full hot harness passes mixer 0/260, biquad 0/300, ADPCM 0/200, and skin 0/2000 differences. All 101 mixer benchmark outputs have the same digest.
- Initial hot-profile setup failures are preserved: missing Capstone, then the recompiler's default Downloads DLL path. An isolated venv with Capstone 5.0.7/pefile 2024.8.26 and explicit `PROTODROID_ENGINE=/home/user/ProtoDroid/prototypeenginef.dll` produced the successful run. No fake reference or fallback binary was used.
- Present-statistics, detailed-diagnostics, performance-hint UBSan tests and performance-evidence parser/comparability tests pass. Historical self-compare is rejected for missing common metrics as required; the initial 60-vs-61 assertion failure is preserved.
- All five targeted AArch64 assembly files generated and final function-extraction assertions/hash checks passed. Earlier target/header/symbol/form assertions and the invalid unconditional shell verification message remain recorded; assembly is not a runtime-correctness test.
- `verify_phase3_records.py` passes raw-row, digest, invalidation, comparability, assembly, and research cross-reference checks. The final 1,281-file comprehensive artifact manifest passes regeneration and independent `sha256sum -c` verification.
- No production code, settings, quality, ABI, or renderer behavior changed.

## 6. Limitations

There is no physical Android device, ADB, GPU profiler, Perfetto, SimplePerf, perf, strace, or Vulkan shader tool. Gprof adds instrumentation overhead and samples host CPU only; inlining hides finer function attribution. RCF gprof call arcs are invalid and excluded; flat profiles include startup/teardown and omit syscalls/uninstrumented libraries. Synthetic DXT, one-voice mixer, synthetic-scale RCF, and offline Bink workloads do not establish their share of actual game frames. RCF cold means process/index-cold with OS cache not dropped, never cold phone storage. Repetition, order, frequency, and cache state materially affect microtimings. Existing Present/DXVK instrumentation has not produced a current device trace; historical heavy tracing can inflate pipeline/frame timings.

The release APK was unavailable at the blocked redirect host. Targeted assembly uses musl and a different compiler/library environment with outlined atomics, not Android/Bionic or release-binary equivalence, and was not executed. Host/historical evidence orders investigation but cannot rank total device frame impact across scenes; that gate remains open.

## 7. Next priority

Phase 4 should first measure indirect-dispatch once/hash and generated TSO overhead using exact differential fixtures and generated-code inspection, then accept only changes that exceed normal variance without weakening ordering, setjmp/longjmp, thread/fiber, diagnostic, or plugin behavior. Keep DXT/RCF/Bink/mixer candidates for their respective later phases. A future physical run must separate lightweight pipeline timing (`pipeline_trace=0`) from heavy structural tracing; device Present percentiles, driver/GPU wait, shader latency, visual correctness, thermals, and sustained pacing remain the primary open gate.
