# ProtoDroid profiling record

Phase 3 ran from `2026-11-03T18:21:53Z` to its `2026-11-03T19:26:15Z` completion reading (**1h04m22s**). Host, targeted-assembly, and historical-instrumentation profiles are recorded below; physical-device total-frame attribution remains unmeasured.

Existing instrumentation available for future measurements:

- Present-to-Present wall intervals with one-second FPS/average/P99/max windows.
- Detailed timing samples with p50/p95/p99/max, fixed long-frame thresholds, and four worst events.
- Per-tour-phase exact Present average/max and rolling-window metrics.
- Process CPU time, available per-core frequency/busy data, thermal status/headroom APIs, and explicit unsupported states.
- Optional guest/import profiling during the isolated diagnostic tour.
- DXVK/native monotonic logs and pipeline tracing; Present wall time is explicitly not GPU time.

Measurement cautions:

- The tour uses a 4x guest clock and is not normal-play performance evidence.
- Rolling one-second windows and tour-phase windows must not be combined as though they were independent raw samples.
- Historical logs from another source revision cannot establish a 0.15 A/B result.
- Host timing and ARM64 CI timing do not establish physical phone FPS or GPU time.

## Phase 3 host profiles

Raw normal/`-pg` logs, `gmon.out`, gprof flat/call graphs, exact commands, hashes, environment, preserved setup failures, test-only harness diffs, JSON, and assembly are under `/home/user/protodroid-016-evidence/profiling/`. The final comprehensive manifest covers **1,281 files / 1,406,612,537 bytes** and passed regeneration plus independent verification; earlier 35-file lists remain separately. Source is `a5c9c22e97fccc6e44c5cceb50dbb4a462e234bc`; no runtime code changed.

| Profile | Raw result | Correctness and interpretation |
| --- | --- | --- |
| Production DXT, 2,000 iterations | Normal decode medians 256.806/266.629/548.775/630.633 µs for DXT1-BGRA/DXT1-A1R5G5B5/DXT3/DXT5. Gprof self time: `DecodeDxt` 58.05%, `DecodeColorBlock` 41.22% over 459,407,360 calls | All four normal/pg digest pairs match. `-pg` overhead ratios are 1.0369/1.0336/1.3494/1.2832. Inlined helpers are not separately attributed; no comparison with the different Phase 1 400-iteration protocol or FPS claim |
| DXT size/scaling sweep | Seven normal batches at 64/256/512/1024/2048 square, 20,000/4,000/2,000/250/64 iterations. DXT1→BGRA medians 3.113/58.191/258.653/1068.621/4274.575 µs; DXT5 8.476/148.375/630.217/2549.066/10072.598 µs. Full four-format table is in `phase-03.md` | Four 512/2,000 digests exactly match the original; graphics UBSan passes. Per-pixel cost rises then plateaus; cache/memory causation is unproven. Different size/iteration digests are separate baselines; no FPS claim |
| DXT destination rotation | Six order-balanced repetitions at 1/4/16/32 destinations. Median paired ratios at 32 slots: DXT1→BGRA 1.007×, DXT1→16-bit 1.018×, DXT3 1.161×, DXT5 1.185×; absolute medians 264.525/269.015/629.148/756.924 µs | All 96 case results match original digests. Supports host destination/store-path sensitivity, not cache causation. x86 and ARM64 DXT3/DXT5 store paths differ; no device transfer |
| Native mixer, unchanged benchmark repeated 50 times | First post-differential benchmark 4.649 µs/call; repeated normal median-of-medians 3.642, `-pg` 3.954, ratio 1.0857. Gprof sampled self time is effectively all `Native_105e8860`, 706,400 calls | Full differential checks have zero differences; all 101 benchmark outputs share `1b1304bebe7b5bc3`. Repetition/cache state matters; first-vs-repeated values are not an optimization delta |

These profiles identify host-side functions for later controlled investigation, not actual game hotspot shares or device optimization targets by themselves.

## Targeted AArch64 assembly baseline

An isolated ziglang 0.17.0 / LLVM 22.1.8 generated assembly from pinned source using `aarch64-linux-musl`, generic ARMv8.2+RCpc, Cortex-A76 tuning, and production-like `-O2`/flat-memory/TSO flags. This is not Android/Bionic or release-binary equivalence; Zig appends outlined atomics, no output was executed/timed, and the release APK download was blocked at its unsupported redirect host. Failed Android-target/header and initial analysis attempts are preserved.

Parsed results: DXT 1,209 instructions with whole-file NEON lane/vector-store forms; native mixer 251 with zero `FMLA/FMADD`; generated mixer 1,368 with 50 `LDAPR` and eight `STLR`; `RecompLookup` 43 with acquire once-flag before hash probing. `RecompDispatch` includes two inlined once/lookup paths and two import-dispatch call sites. Instruction counts include cold paths where noted and are not cycle costs. Phase 4 must measure dispatch/TSO candidates with differential tests; no assembly-only adoption.

## Historical instrumentation guidance (revision 51e51, not baseline/A-B)

A read-only parser and exact excerpts are under `profiling/historical-instrumentation/`. It verifies matching native/journal source IDs, 171 graphics calls/requests with trace sequences 0–170 and zero errors, and 342/342 stages. Sequence 6 has two unavailable zero-byte SPIR-V records, not malformed-shader proof. Excluding them: 80 unique vertex hashes, 87 fragment hashes, 97 known VS/FS pairs across 170 creations, 73 repeated-pair instances, maximum multiplicity six; state can still differ. The DXVK log lacks a source ID and the trace lacks timestamps/source IDs; those limitations remain explicit.

| Historical signal | Measurement | What it does and does not establish |
| --- | --- | --- |
| Graphics pipeline compilation | Approx. 8,644.179 ms driver-call total, 50.550753 ms weighted mean, 154.41 ms max; approx. 4,613.235 ms compile-mutex wait, 26.977981 ms mean, 204.658 ms max across 171 calls | `driver_*` wraps `vkCreate*Pipelines`; `wait_*` is compile-mutex acquisition. Relaxed resetting counters/active-compilation-triggered emission make aggregates approximate and prevent direct frame alignment. Heavy shader dumps were enabled inside the lock for all 171 calls, so trace/fsync overhead may inflate timings. Not equivalent render-thread stall time |
| Worst historical stall window | Present interval 1,032.764 ms; EngineService max 1,036.180 ms; Present-wall max 1.129 ms; zero reported DXT/I/O | The measured Present API and DXT/I/O counters do not explain this window. No GPU/CPU/pipeline cause is proven |
| Early streaming windows | Up to 99.13 ms DXT work (2,174 calls) and separately up to 99.9 ms read I/O (50 reads) in rolling ~1 s diagnostics | Preserve DXT/I/O instrumentation and cold/warm separation; rolling windows are non-independent and not normal-play FPS evidence |

Current provisional investigation order is (1) first-use pipeline/loading attribution, (2) early streaming DXT/RCF I/O, (3) sustained EngineService/Present pacing and threads, (4) movie-only Bink decode/registered copies, and (5) mixer unless device audio/CPU evidence raises its priority. This is not a total-frame-impact ranking and adopts no optimization. Future device timing should use lightweight pipeline timing with `pipeline_trace=0`; heavy structural tracing belongs to a separate, non-comparable run.

Evidence hierarchy behind that order:

1. **Historical in-app signals, non-causal:** actual pipeline/Present/DXT/I/O counters exist, but they come from `51e51`, use resetting/non-frame-aligned windows, and pipeline timings include heavy trace/fsync work. The worst Present/EngineService correlation and early DXT/I/O windows cannot be summed or assigned a cause.
2. **Controlled host costs, no frame denominator:** DXT scaling/destination rotation, RCF cold/warm paths, corrected Bink decode/registered copies, and mixer profiles establish reproducible CPU workloads. Different scenes, units, and harnesses cannot produce a numeric cross-workload bottleneck ranking.
3. **Structural target evidence:** AArch64 assembly confirms NEON, ordered generated-memory, and dispatch/once/hash forms for later exact experiments; instruction counts are not cycle shares.
4. **Missing gate:** no current physical-device scene has common Present percentiles, pipeline timing, thread activity, visual correctness, thermal state, and sustained pacing. Device total-frame impact remains unranked rather than inferred from host speed.

## Measurement-tool validation

`present_stats_test`, `diagnostics_test`, and `perf_hint_test` pass UBSan; `tools/test_perf_evidence.py` passes fixture, real-log, mixed-revision, and comparability guards. The historical summary has 61 Present-wall records and 20 tested phases. A labeled self-compare is `comparable=false` because the old journal lacks common per-phase performance metrics, so it is not an A/B dataset. The parser's `present_windows` JSON contains Present API wall duration, not Presented-frame intervals; current per-phase frame metrics are separate. An incorrect initial 60-record summary assumption and the actual 61 are preserved under `profiling/instrumentation-validation/`.

These are instrumentation-logic checks, not hardware FPS/timing validation.

## Current RCF/Bink host profiles

Persistent commands, hashes, raw logs, gmon files, harness diffs, and failure records are under `profiling/rcf-bink-gprof/`. The real minimal FFmpeg n7.1.1/Bink sample stack is restored; unmodified UBSan suites pass at 32,000 and 44,100 Hz. All normal/pg and cross-harness data digests match.

Each timing cohort has 50 process/index-cold runs and 10×20,000 warm archive open/48-byte-read/close operations per build. OS/storage cache is not dropped. Values are median (p95); full and focused harnesses are not timing-comparable.

| Harness/cohort | Normal | `-pg` | Median overhead ratio |
| --- | ---: | ---: | ---: |
| Full, cold index ms | 13.340700 (14.148600) | 14.441200 (15.172000) | 1.082492 |
| Full, warm µs/lookup | 17.627550 (18.011500) | 17.747850 (19.790700) | 1.006825 |
| Production `path.cpp` + `bink_media.cpp`, cold ms | 18.536700 (20.400800) | 19.445050 (21.458900) | 1.049003 |
| Production `path.cpp` + `bink_media.cpp`, warm µs/lookup | 12.519750 (12.863000) | 12.913600 (13.312400) | 1.031458 |

Deep flat PC profiles add 500 index-cold runs and 2,000,000 production-path warm lookups. Cold reachable self samples are led by archive hashtable find (41.24%) and Bink `Normalize` (14.95%); production-path warm samples include Bink `Normalize` 16.98%, `HostPath` 13.84%, hashtable find 11.95%, `OpenFileSource` 5.03%, path `Normalize` 3.77%, and `GuestCurrentDir` 1.26%. These are raw total-process percentages with startup/teardown/harness exclusions documented, not wall-time fractions or phone attribution.

Optimized RCF gprof call arcs are invalid (including unreachable `CheckPublic`, `MemorySource::Read`, and repeated `BuildIndex` arcs) even non-PIE; no call counts/ms-per-call are used. Missing-zlib/include-path failures and invalid fixture-dominated/PIE profiles are preserved. The standalone `HostPath` stub is not production attribution. No cache/path/archive/Bink optimization is adopted or rejected.

Corrected memory-backed Bink decode cohorts each contain 20 processes × 291 frames. Medians across per-run summaries are unregistered decode total 545.069728 ms / frame median 1.117999 ms / per-run p95 4.490041 ms / p99 5.052517 ms; two-registered-set total 677.875458 ms / median 1.576090 / p95 4.952626 / p99 5.538735 ms. Both have digest `d68445b7a59b9b4a`, per-frame input integrity, and successful YV12 export; registered planes in both sets match every exported frame. These are separate offline host cohorts, not pooled percentiles, pure upload-cost isolation, playback, or FPS.

An earlier 20-run decode cohort is invalid because its test stack `0x400000` overlapped the large input from `0x100000`; input changed at frame 1, offset 3,145,712. Its old digest/timings and resulting frame-73 registered failure are excluded. Corrected sources allocate a dedicated 64-KiB guest stack and recheck input after every frame. Raw invalid and fixed evidence is retained under `profiling/rcf-bink-gprof/`.

## Phase 6 synchronization and Present bookkeeping checks

A 480-process exact-header wait/notify cohort rejected bounded auto/semaphore notification: the multi-object-safe candidate was slower in all 16 pairs for every four/eight-worker single-release scenario (1.453210–2.231109× baseline median), while batch cases were neutral/noisy. These are two-CPU host model results, not device thread attribution.

Actual `PresentStatsTracker` measurement retained 550,872 per-call samples with detailed collection disabled. Ordinary median cost was 25 ns at nominal 60–10,000 Present/s. Once-per-window p99 work was 127.5 ns median at 60 Hz and 10.604 µs median at 10,000 Hz (21.023 µs maximum across 36 windows). This does not justify a bookkeeping rewrite or establish an Android frame-time share. Raw CSV and summaries are under `phase-06/present-stats-*`.

### Phase 6 historical attribution limits

The supplied `proto-native.log` has no CreateEvent/WaitForSingleObject/WaitForMultipleObjects/ReleaseSemaphore records, so its Android run cannot rank the synthetic host wait scenarios as real device hotspots. Its 61 detailed Present-outlier lines contain 241 retained entries after excluding the diagnostics logger: 240 on TID 5192 and one on 5147. TID 5192 requested priority 0, while the final ADPF line reports 3,130 cumulative reports with zero report/target errors and no TID roster. This motivates future roster/lifetime instrumentation, not a claim that ADPF is broken or that explicit registration improves FPS. The log and its summary are historical diagnostic-tour evidence, not released-0.15 or candidate-0.16 A/B data.

## Phase 7 RCF open profile

Actual `OpenFileSource` + `path.cpp` warm exact opens using the 18,044-entry generated RCF had a 12.008622 µs baseline median in the new 36-process/arm cohort; direct archive-only opens were 2.9875725 µs. The basename-deferral candidate was not separable from A/A in paired rounds, so this is a negative optimization result rather than a new bottleneck claim. Cold full-path indexing/open was about 20 ms in this generated fixture and was unchanged by the candidate. These host figures are not Android startup or streaming timings and have no measured per-frame frequency.

## Phase 7 real multi-wait call-site relevance

Read-only `prototypeenginef.dll` SHA `d331be205bdde99897952ee88261ab7e63c5c65e36a4660e2a367886a3537071` has three direct `WaitForMultipleObjects` import calls in the static census: one constant n=2/any/infinite site (`10abdb6d`) and two dynamic-count/any/infinite sites (`10abbad6`, `10abbbc5`). The adopted inline/sized-vector path therefore covers a confirmed real two-handle shape and all observed direct modes. This is static relevance only: no dynamic call count, blocking share, device timing, or FPS/pacing effect is inferred.

The separate 24-process cold RCF audit measured 18.7939795 ms median cold indexing/open and 18,620-KiB whole-process median maxRSS for the 18,044-entry fixture. Exact/basename entry duplication is visible in source, but these numbers do not isolate it or establish Android pressure. No dedup/pointer patch is justified without that missing evidence.

## Phase 7 regenerated current-host evidence

After the continuation store loss, a fresh actual-handler cohort measured candidate/baseline ratios 0.323330–0.773926 over 134.4 million calls, with 16/16 paired wins per case and n=2/any at 0.458088. Fresh normal/ASan/UBSan/TSan stress and RCF semantic fixtures also pass. Absolute nanoseconds from this host must not be compared with the pre-recreation host as an optimization; only the new raw cohort is currently reopenable. No fresh RCF timing A/B or Android measurement was performed.

## Phase 8 D3D9 bridge profile

Static source attribution found two acquire loads in every `HostTable::Get`, a local-static guard in every `SetSamplerState` config read, and two local-static guards in every `MaybeScreenshot` default Present path. Exact ARM64 fixtures show the respective `ldar`/`ldarb` operations; these are static operation counts, not device timing. Balanced host fixtures measure no x86-64 HostTable delta (candidate 1.006302, A/A 1.005189), a non-eliminated anisotropy config-read ratio of 0.773923 (A/A 0.993443), and a screenshot default-path ratio of 0.981703 including unchanged real `shot_now` polling (A/A 0.990451). None is a frame-time or physical-device result. Real D3D9 call frequencies and DXVK driver time remain unmeasured.
 Independent confirmation measured anisotropy 0.784688 (36/36 wins) and screenshot 0.991820 (22/36); anisotropy is adopted, while the screenshot config move is rejected as immaterial.
 Combined 60-pair medians are 0.779120 for anisotropy (60/60) and 0.988630 for screenshot (42/60); the latter saves only 0.230 ns/Present on confirmation and is rejected on absolute materiality.

Actual `ubuntu-24.04-arm` fixtures then supplied two 24-round native ARM64 cohorts. HostTable combines to 0.999926 (25/48; A/A 1.000417), neutral even with its candidate load present, so `9a65eb9` restores the original acquire. Anisotropy combines to 0.375547 (48/48; A/A 1.000090), with final median 13.303625 ms versus 35.426306 ms over 20 million calls and 144 final raw processes independently recomputed. These are fresh-process, `g++ -O2 -std=c++20` function fixtures, not game scenes, shader-cache tests, phone frame times, or physical-device FPS. Real D3D9 call frequencies remain unknown, so no end-to-end speedup is inferred.

## Phase 9 cache-loading profile

The patched DXVK constructor reads/validates cache entries, its writer appends and flushes on a dedicated thread, and cached pipeline warmup is queued to a background worker. The ordinary native mirror runs every 30 seconds on a helper thread; baseline `BackupStateCache` read and rewrote the entire populated source through a 16 KiB buffer regardless of whether either file changed. Thus redundant unchanged-backup I/O was source-established. Its actual phone/shared-FUSE cost and cross-device cache distribution remain unmeasured. Pipeline wait/driver timing already exists behind `PROTODROID_PIPELINE_TIMING`; no new device profiler result is claimed.

The adopted byte-validated tracker removes unchanged writes after comparing both complete files. Native ARM64 synthetic-size unchanged ratios are 0.075922–0.111711, all 12/12; changed timing is inconclusive. A tracked real 179,336-byte Prototype cache contains 1,014 SHA-1-valid entries with 134–174-byte payloads. At that exact fixture, host unchanged ratio is 0.068241 with 338.860 µs median saving, while changed ratio is 1.030809 and inconclusive. Independent real-fixture host verification passes 344/344 and cache structural/hash verification 3,058/3,058. Final real-fixture ARM64 is 0.073964 (12/12) with 443.623 µs median saving; changed is 1.002738 (6/6), inconclusive. Independent final Check recomputation passes 547/547 over 360 rows. These are warm host/native-CI file-I/O fixtures, not phone loading or FPS measurements.

The tracked historical `pipeline_trace.log` is also directly reopenable. A Phase 9 read-only audit passes 713/713 checks and regenerates 171 graphics pipeline calls, 342 stages, 80 unique VS/87 unique FS hashes, 97 unique known pairs, and 73 repeated-pair instances, with zero logged request errors. Rounded timing averages reconstruct approximately 8,644.1787 ms graphics driver and 4,613.23474 ms wait time. This is historical source `51e51ea3` diagnostic-tour evidence, not a new 0.16 device run; SPIR-V sizes are not D3D9 SHA-1 inputs, and pair repetition is not concurrent duplicate-registration proof.

The post-recovery extended trace/source audits add 378/378 and 30/30 passing checks. Full logged descriptions form eight duplicate-like groups/nine extra calls, but existing DXVK dedup includes specialization IDs/values under a mutex and the trace omits those values and object/cache lifetimes. This is not a measured redundant-compilation saving. The real-cache v18 audit passes 8,434/8,434 checks: zero exact duplicate entries, 262 shader pairs, and 303 groups/311 extra entries differentiated only by specialization data. It confirms that a text-only dedup key is unsafe; it does not measure loading time or justify prefix compression. A fresh recovered-Check verifier also recomputes 390/390 checks over all 360 final ARM64 raw rows; the earlier 547/547 result remains the historical verifier record.

## Phase 10 Present diagnostic profile

The baseline Android presenter performs three steady-clock reads and two atomic counter additions on every successful queue pre-acquire even with `DXVK_PRESENT_STATS` disabled; its application-acquire fallback has three clocks/three counter updates. Exact target-function host timing measures disabled queue pre-acquire medians 106.172747 ns baseline versus 9.096117 ns candidate, paired saving 97.064736 ns over 24/24 rounds; application fallback saves 103.675612 ns. Native ARM64 run `41836560344` independently verifies 210 checks/48 rows: 116.472338/13.836171 ns, saving 102.642780 ns, A/A 0.999282, enabled 1.000611. Nine source-identity checks prove the partial-baseline measured parts identical to the restored all-16-patch source; only unrelated include/FIFO code differs. The saving is 0.000102643 ms/Present, 0.006159 ms CPU/s at 60 presents/s and 0.000616% of a 60 Hz interval. **Rejected as immaterial and reverted**; these are fake-Vulkan diagnostic CPU fixtures, not GPU submissions, Android Present latency, FPS, or pacing.

The read-only historical Present audit passes 1,229 checks and exactly reconstructs the 3,131-interval/3,132-wall-call summary. Rounded weighted interval/wall averages are 19.525278/0.838314 ms. Twenty-seven intervals but only three wall calls exceed 50 ms; thirteen >50 ms-interval windows and three >100 ms-interval windows have wall max below 8.5 ms. Window-average interval correlation is 0.783964 with EngineService and −0.211326 with wall. These historical diagnostic windows do not support FIFO/image-count/frame-latency/lock changes, but they also do not identify the non-Present engine/draw/lock/GPU cause. No global frame percentile or causal GPU attribution is inferred.

The restored all-16-patch memory/submission audit passes clean patch application plus unified-memory, depth-resolve, FXAA, device-preflight, and BC tests. Managed-copy release already defaults on for Android; submission/finish locks protect lifetimes; timed memory maintenance uses a 500 ms deadline; resource timing wraps only actual waits. No workload supports a safe deletion or rewrite, so H3 makes no source or performance claim.
