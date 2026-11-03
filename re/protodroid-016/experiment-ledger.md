# Experiment ledger

Every experiment records hypothesis, exact baseline/candidate revisions, changed files, environment, settings, scene, cache state, raw result, correctness result, and `adopt`, `reject`, or `inconclusive`.

## B0 — 0.15 baseline capture

- Hypothesis: existing fixtures can establish a reproducible host reference without making a device-performance claim.
- Baseline: `18ba66bd0945e405aaded0de5ff8848a8cd07932`
- Candidate: none
- Changed runtime files: none
- Environment: x86_64 Linux, two logical CPUs; no attached Android device
- Scene/cache: deterministic host fixtures; physical scenes not executed
- Raw results: historical `/tmp/protodroid-016/baseline` artifacts were lost during sandbox recreation; summarized historical values remain in `baseline.md` and are not represented as currently available raw logs
- Correctness: all host differential/UBSan fixtures passed; exact-0.15 ARM64 CI correctness was already green
- Decision: host/CI reference established; physical-device baseline remains unmeasured. Baseline only, not an optimization adoption
- Additional evidence decision: mixed-source `TestingFiles` logs were rejected as one run; see `rejected-changes.md`

## A2-BLK — Black-mobiu shader/cache behavior probe (audit, not an optimization A/B)

- Hypothesis: determine whether the pinned implementation supplies asynchronous shader translation or a directly reusable DXVK cache mechanism; no FPS gain was assumed.
- Exact source/files: Black-mobiu `eaab755566185f4d271c71470d6b35d26a4e59aa`, unchanged `vulkan/vk_shader.cpp`; external harness/stub revisions and all diffs are hashed under `audit/tests/black-mobiu/`.
- ProtoDroid baseline/candidate/runtime changes: none.
- Environment/settings/scene/cache: x86_64 Linux/GCC 12.2.0; stubbed glslang; direct `Compile` calls outside the production module-map caller; cold/warm and 64-key stress cache states; normal, ASan/UBSan, and TSan. No Vulkan rendering.
- Raw result: six orchestrated cold misses caused six compilations with `max_active_compilations=1`; warm hit avoided compilation. Final 64-key stress totals were 77/78/79 compilations (scheduling-dependent). A four-byte opaque entry was accepted and reloaded on a second call without compilation.
- Correctness: corrected final runs pass; initial stub/expectation failures and packed-`vector<bool>` TSan harness failure are preserved. No real HLSL, SPIR-V, module validity, rendering, or caller-level duplication result.
- Decision: **reject** the claimed async-translation/cache-format import at audit; ProtoDroid shader/loading performance remains **inconclusive**.

## A2-GDFX — PrototypeRecomp host-I/O behavior probe (audit, not an optimization A/B)

- Hypothesis: test whether guest `PENDING`/APC behavior implies host asynchronous reads or a locking/index model worth importing.
- Exact source/files: PrototypeRecomp `c9224ecd1b7c422d5fa498c5fd541fabeb38858f`, unchanged `runtime/xbox/gdfx_volume.cpp`; its `scripts/test_gdfx_volume.cpp` plus hashed custom `--wrap=pread` probe.
- ProtoDroid baseline/candidate/runtime changes: none.
- Environment/settings/scene/cache: x86_64 Linux/GCC 12.2.0; deterministic synthetic volume, not the 134-byte LFS pointer or original game data; irregular reads, eight-thread reads, synthetic-delay one/four-thread comparison, and missing-backing-file case; normal, ASan/UBSan, and TSan.
- Raw result: 8,388,745 payload bytes matched across 617 irregular reads; eight threads issued 1,024 observed 16 KiB reads with zero mismatches and `max_active_pread=1`. Equal-work delay runs were 17.845/18.587 ms (ratio 1.042). Missing backing data returned 4,096 zero bytes with full success length.
- Correctness: traversal, EOF, chunk-sweep, byte identity, and sanitizer checks pass; synthetic fixtures and intentional zero-fill behavior are not authentic-content or corruption-validation proof.
- Decision: **reject** host-async-I/O/global-lock/index import at audit; real ProtoDroid loading benefit remains **inconclusive**.

## A2-REF — GoW III/ISS/Bloodborne focused applicability checks

- Hypothesis: execute available focused reference tests before ranking their mechanisms; passing tests would establish selected implementation behavior only, not transfer FPS to ProtoDroid.
- Exact sources/files: GoW III `b3a7c3d5e916cf8f4622192c0faa9383ebd77c00`, ISS `43c833deb37bc504bf099bbf707a5440103a8cae`, Bloodborne `f241353e36f46bf183806acd7b79222afff2fd2f`; exact test paths and commands are in `repository-ledger.md` and `audit/tests/commands.txt`. No external production source or ProtoDroid runtime file changed.
- Environment/settings/scene/cache: x86_64 host, GCC 12.2.0, Python 3.11.2, isolated Unicorn 2.1.4; synthetic/native fixtures, including UMA 256/2048 KiB configurations. Page-state tests use the real permission enum/PageState with only the unused Boost `interval_set` type stubbed.
- Raw result: GoW III 6/6 native plus 20/20 Python/helper checks; ISS 6/6 native; all 12 GoW/ISS native tests pass ASan/UBSan. Bloodborne 3/3 AOT and 2/2 normal UMA configurations pass with zero lost writes; fresh normal-build longest compaction/demotion-hammer freezes were 12.929/0.51 ms and 17.588/11.98 ms. AOT native/strict pass ASan/UBSan; variants fails LSan on two intentional-lifetime `HookRec` allocations and separately passes UBSan-only, without waiving the leak result. Unmodified UMA UBSan stops on the initial zero-length null-`memmove`; a separately labeled test-only guarded copy then completes both UBSan configurations with zero lost writes. Original UMA TSan fails on the fixture stop flag; the atomic-stop fixture then retains an unresolved writer/`MAP_FIXED` model report, so no clean UMA TSan result or confirmed production race is claimed. Separate TSan outcomes and all setup/fixture failures are preserved in the phase/ledger records.
- Correctness: no Android, phone GPU, full renderer, authentic game, or FPS validation. GoW/ISS lineage and near-duplicate tests are not independent confirmation. Pipeline-binary tests remain unrun.
- Decision: reference behavior established only where stated; all technique adoptions remain **none**, candidate performance remains **inconclusive**, and incompatible imports remain rejected at audit.

## B2-CACHE — Existing ProtoDroid/DXVK baseline-equivalence check

- Hypothesis: establish that the existing state-cache/memory/capability behavior passes its focused host gates before any later performance change.
- Exact source/files: ProtoDroid `5eb5f1778ce68ef7163fedb70a6503793735c037`, `src/native/state_cache.cpp`, `src/native/tests/state_cache_test.cpp`, and ordered `android/patches/dxvk-*.patch`; DXVK base `c707d9026f33b6ab89639f154b6ac5f6326fa037`. Base, ordered patch hashes, and combined patched-diff hash are in `audit/tests/local-state-cache/`.
- Candidate/runtime changes: none; patches were applied only to a disposable DXVK copy and the reference clone stayed clean.
- Environment/settings/scene/cache: CI-equivalent UBSan compile/run, plus five existing source-level checks; resolution/migration/atomic-backup/entry-layout/flush/drain cache cases. Initial sparse patch-check failure is preserved.
- Raw result: state-cache test and unified-memory/depth-resolve/FXAA-source/device-preflight/BC-support checks pass. No timing delta was measured.
- Correctness: host/source correctness only; glslangValidator is unavailable, so FXAA SPIR-V compilation and full CI/Vulkan/device validation did not run.
- Decision: **baseline established for these focused gates; no optimization adopted or rejected by timing.**

## P3-DXT — Production DXT gprof attribution

- Hypothesis: a symbolized host profile can identify which production DXT functions dominate this deterministic workload before any decoder experiment is considered.
- Baseline/candidate: profiling source `a5c9c22e97fccc6e44c5cceb50dbb4a462e234bc`; matched normal and `-pg` builds, no candidate implementation and no runtime file changed.
- Exact files: `src/native/d3d9_dxt.cpp`, `d3d9_dxt.h`, `tests/dxt_benchmark.cpp`; source and binary hashes under `profiling/dxt-gprof/`.
- Environment/settings/scene/cache: x86_64 Linux, GCC 12.2.0/gprof 2.40, `-O3 -DNDEBUG -g`, `-pg` as the only instrumentation addition, 2,000 iterations, seven batches, no affinity/LTO/sanitizers. Deterministic 512×512 synthetic textures; no device scene or shader cache.
- Raw result: normal decode medians 256.806/266.629/548.775/630.633 µs; `-pg` 266.289/275.575/740.528/809.197 µs. Gprof self time: `DecodeDxt` 58.05%, `DecodeColorBlock` 41.22%, 459,407,360 calls. Raw logs/gmon/profile are persistent.
- Correctness: all four normal/pg digest pairs match exactly. Inlined helpers are not separately attributed; instrumentation overhead is substantial for DXT3/5.
- Decision: **inconclusive for optimization; profile recorded**. No patch, FPS, actual-game share, or comparison with Phase 1's different 400-iteration protocol is claimed.

## P3-DXT-SCALE — Decoder size/working-set characterization

- Hypothesis: a single 512-square synthetic decode does not describe per-call/per-pixel scaling across texture sizes; digest-compatible larger/smaller workloads can provide a more useful future candidate baseline.
- Baseline/candidate: same production `d3d9_dxt.cpp` at `a5c9c22e97fccc6e44c5cceb50dbb4a462e234bc`; no optimization candidate. A test-only sweep harness adds validated runtime dimensions and prints size while preserving the original 512/2,000 behavior.
- Exact files: `profiling/dxt-gprof/dxt_benchmark_sweep.cpp`, `test-only-sweep.diff`, `dxt-sweep-normal`, `dxt-size-sweep-normal.log`, `dxt-size-sweep.json`, `dxt-size-sweep-summary.txt`, `sweep-commands.log`, and `sweep-artifact-hashes.txt`.
- Environment/settings/scene/cache: same x86_64 host and normal `-O3 -DNDEBUG -g` build, seven batches, deterministic random input, warm repeated decode per case. Sizes 64/256/512/1024/2048 square; iterations 20,000/4,000/2,000/250/64. No device, game scene, upload, rendering, or cold-cache claim.
- Raw result: median µs/call rows are 64: 3.113/2.961/8.058/8.476; 256: 58.191/61.647/133.512/148.375; 512: 258.653/261.281/540.566/630.217; 1024: 1068.621/1087.312/2224.180/2549.066; 2048: 4274.575/4346.024/8898.198/10072.598 for DXT1→BGRA/DXT1→16-bit/DXT3/DXT5. Per-pixel cost rises then approaches a plateau; compressed DXT1→BGRA throughput is 467.9 MiB/s at both 1024 and 2048, DXT5 392.3/397.1 MiB/s. The repeated 512 medians differ from the earlier same-binary run by +0.72%/-2.01%/-1.50%/-0.07%.
- Correctness: all four 512/2,000 digests exactly match the original (`1947643`, `3898345`, `5683106`, `7451392`); all 20 parsed rows satisfy protocol assertions; production graphics UBSan passes. Different dimensions/iteration counts have separate deterministic cumulative digests and are not expected to match one another.
- Decision: **inconclusive for optimization; retained scaling baseline**. Cache/memory-working-set influence is a hypothesis without hardware counters, not a proven cause. No production patch adopted/rejected and no FPS/phone-throughput claim.

## P3-DXT-DST — Destination working-set and store-path sensitivity

- Hypothesis: repeated decoding into one destination can hide destination-working-set costs; rotating destinations with identical source/mutations can test sensitivity without changing decoded output.
- Baseline/candidate: same production decoder at `a5c9c22e97fccc6e44c5cceb50dbb4a462e234bc`; no runtime optimization. Test-only harness rotates 1/4/16/32 destination buffers with identical pointer-selection logic across configurations.
- Exact files: `profiling/dxt-gprof/dxt_benchmark_dst_sweep.cpp`, `test-only-dst-sweep.diff`, `dxt-dst-sweep-normal`, `dxt-dst-sweep-normal.log`, `dxt-dst-sweep.json`, `dxt-dst-sweep-summary.txt`, commands/hashes, and preserved initial transformation failure.
- Environment/settings/scene/cache: x86_64 normal `-O3 -DNDEBUG -g`; 512×512, 2,000 iterations, seven batches/process. Six repetitions: three ascending then three descending slot orders. Source and mutation sequence are constant; each decode fully overwrites the selected destination. At 32 slots destination storage is 32 MiB for BGRA and 16 MiB for DXT1→16-bit. No device, actual texture stream, upload, or rendering.
- Raw result: median µs/call and median paired ratio to the same repetition's one-slot value: 1 slot 260.346/264.839/554.143/640.221 (1.000×); 4 slots 261.839 (1.010×)/265.402 (0.999×)/620.890 (1.120×)/695.671 (1.085×); 16 slots 263.098 (1.014×)/267.291 (1.009×)/620.726 (1.119×)/699.877 (1.102×); 32 slots 264.525 (1.007×)/269.015 (1.018×)/629.148 (1.161×)/756.924 (1.185×), ordered DXT1→BGRA/DXT1→16-bit/DXT3/DXT5. DXT3 paired ranges at 4/16/32 are 1.082–1.150/1.085–1.158/1.129–1.388; DXT5 1.058–1.113/1.069–1.107/1.041–1.292. DXT1 remains near unity.
- Correctness: all 24 processes and 96 case results match the required original digests `1947643`, `3898345`, `5683106`, `7451392`. Initial source transformation failed before build/run and is retained. Source inspection distinguishes x86 DXT1 direct row stores from x86 DXT3/DXT5 generic per-pixel stores; ARM64 DXT3/DXT5 uses a different NEON full-tile path.
- Decision: **host destination-working-set/store-path sensitivity supported; cache causation and optimization remain inconclusive**. Ascending/descending persistence does not eliminate frequency/order variability or prove a hardware mechanism. No production change adopted/rejected and no ARM64/device/FPS transfer.

## P3-MIX — Native mixer gprof attribution and repetition sensitivity

- Hypothesis: benchmark-only profiling can attribute native mixer CPU cost without differential-test setup dominating the samples, while matched digests verify equivalent work.
- Baseline/candidate: source `a5c9c22e97fccc6e44c5cceb50dbb4a462e234bc`; normal and `-pg` builds of unchanged production `hot_audio.cpp`/`hot_skin.cpp`, no candidate implementation.
- Exact files: original `hot_audio_test.cpp` plus retained test-only copy/diff adding an early 50-repeat benchmark-only branch and include-form change; generated reference/pages from unchanged `prototypeenginef.dll` under `profiling/hot-audio-gprof/`.
- Environment/settings/scene/cache: x86_64 host, production `-O2` flags plus `-g`, `-pg` only for instrumented build, isolated Capstone 5.0.7/pefile 2024.8.26, explicit root DLL. Unchanged one-voice/three-channel/three-output 256-sample benchmark; first post-differential run versus 50 repeated benchmark-only runs recorded separately.
- Raw result: first post-differential 4.649 µs/call; repeated normal median-of-medians 3.642, `-pg` 3.954, ratio 1.0857. Gprof sampled self time effectively all `Native_105e8860`, 706,400 calls. Missing-Capstone and missing-default-DLL setup failures are preserved before the successful run.
- Correctness: unmodified full differential harness passes mixer 0/260, biquad 0/300, ADPCM 0/200, skin 0/2000; all 101 benchmark outputs share `1b1304bebe7b5bc3`.
- Decision: **inconclusive for optimization; repetition/cache sensitivity recorded**. The first/repeated difference and historical 4.830 µs/call are not represented as a 0.16 improvement; internal inlined helpers remain unranked.

## P3-ASM — Targeted AArch64 generated-code and dispatch inspection

- Hypothesis: pinned production/generated sources can be inspected at instruction level with an honest cross-target compiler when the release APK and local NDK are unavailable, exposing structural candidates without inferring timing.
- Baseline/candidate: `a5c9c22e97fccc6e44c5cceb50dbb4a462e234bc`; no optimization candidate or runtime change. Isolated ziglang 0.17.0 / LLVM 22.1.8 generates assembly only.
- Exact files: `profiling/arm64-assembly/{d3d9_dxt,hot_audio,recomp-hot-reference,runtime,imports}.s`, `analyze_assembly.py`, `assembly-analysis.{json,txt}`, build/command logs and hashes. Generated input is the retained `hot-audio-gprof/ref.c` from unchanged `prototypeenginef.dll`.
- Environment/settings/scene/cache: `aarch64-linux-musl`, generic ARMv8.2+RCpc, Cortex-A76 tune, production-like `-O2`, flat memory; generated C additionally `RECOMP_TSO`. Zig appends outlined-atomics mode. No execution, scene, cache, Android/Bionic, NDK-byte-identity, or timing. GitHub release-asset redirect and local NDK/QEMU were unavailable.
- Raw result: DXT exported 1,209 instructions; whole file 12 `LD1` lane forms/four vector `STR Q`/zero `ST1`. Native mixer 251 instructions, zero `FMLA/FMADD`; generated mixer 1,368, 50 `LDAPR`, eight `STLR`, zero `FMLA/FMADD`. `RecompLookup` 43 instructions with acquire once-flag before multiply/shift hash probing; `RecompDispatch` 221 with two inlined once/lookup paths and two static import-dispatch calls. `DispatchImport` 493 includes cold/error/profiling paths.
- Correctness: all five assembly files generated and function-extraction assertions passed after preserving initial target/header failures, a wrong mangled-name assertion, an `ST1`-only assertion, and an invalid unconditional shell verification message. Final hash verification was fail-fast and passed. Assembly checks do not establish runtime or visual correctness.
- Decision: **structural baseline only; inconclusive for optimization**. Phase 4 should measure indirect-dispatch once/hash and TSO costs with exact differential tests. No instruction-count-to-cycle/device conversion and no adoption.

## P3-HIST — Historical instrumentation coverage and stall attribution limits

- Hypothesis: the revision-identified historical tour can reveal which existing counters are useful and which attribution gaps must remain open, without being misused as a 0.15/0.16 A/B.
- Baseline/candidate: historical source `51e51ea3b20927e40e94135a6a09633c616dcf61`; read-only analysis only, no candidate or runtime change.
- Exact files: root `proto-native.log`, `journal.log`, `app_process64_d3d9.log`, `pipeline_trace.log`, and `android/patches/dxvk-zzzzzzzzz-pipeline-timing.patch`; hashes/parser/JSON/excerpts under persistent `profiling/historical-instrumentation/`.
- Environment/settings/scene/cache: historical Mali-G720 diagnostic tour, `manhattanTest NoAutoSave`, isolated tour cache/saves, render 2400×1080 in 2712×1220 window, FIFO, default fps_limit 0, tour 4× clock; not normal play and not a current device run. State-cache log reports 195 valid entries read; no cache-state inference beyond that line.
- Raw result: 171 graphics calls/requests, zero errors, trace sequences 0–170 and 342/342 stages. Sequence 6 has two unavailable zero-byte SPIR-V records. Excluding them: 80 unique vertex hashes, 87 fragment hashes, 97 known VS/FS pairs over 170 creations, 73 repeated-pair instances, max multiplicity six. Approximate graphics driver total 8,644.179 ms / weighted mean 50.550753 / max 154.41; compile-mutex wait total 4,613.235 / mean 26.977981 / max 204.658. Eight compute calls, approximately 57.558 ms driver total. Worst window: 1,032.764 ms Present interval, 1,036.180 ms EngineService, 1.129 ms Present wall, zero DXT/I/O. Separate rolling windows reach 99.13 ms DXT and 99.9 ms read I/O.
- Correctness: parser asserts native/journal source IDs, trace sequence/call agreement, summary structure, and worst-window values. Patch inspection establishes `wait` as compile-mutex acquisition and `driver` as Vulkan driver-call time. Relaxed reset snapshots and non-fixed emission make totals approximate; DXVK/trace source-ID limitations are explicit.
- Decision: **inconclusive for causal optimization; instrumentation priorities recorded**. Do not claim that pipeline compilation caused the worst frame, that Present wall is GPU time, or that rolling windows are independent samples. Heavy shader dumps were enabled inside the compile lock for all 171 calls, so trace/fsync overhead may inflate driver/wait/frame times; future lightweight timing uses `pipeline_trace=0` and heavy tracing is separate. No technique adopted/rejected.

## P3-RCF — Cold index and warm archive-path profiling

- Hypothesis: the real Bink/RCF source path can be measured separately for first-process indexing and repeated warm archive open/read/close, revealing whether later memory/I/O work should prioritize index construction or lookup/path overhead.
- Baseline/candidate: source `a5c9c22e97fccc6e44c5cceb50dbb4a462e234bc`; matched normal/`-pg` profiles only, no candidate implementation or runtime file changed.
- Exact files: production `bink_media.cpp`, `bink.cpp`, `bink_blit.cpp`, `path.cpp`, original `bink_test.cpp`, and evidence-only full/focused harness copies/diffs under `profiling/rcf-bink-gprof/`. FFmpeg n7.1.1 `db69d06eeeab4f46da15030a80d539efb4503ca8`; zlib v1.3.1 `51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf`; sample repo `48bd8df605e36f4c66d365acb9843ac77bb76cf5`, sample SHA-256 `e4cc59c1242615fc33159537e21d019599481676f1de62818a5cc4aa9d2f2dd5`.
- Environment/settings/scene/cache: x86_64 host, real 15,563,652-byte Bink sample, five-archive/18,047-path fixture. Full `-O2` and focused `-O2 -ffunction-sections -fdata-sections/--gc-sections` builds; `-pg` only for instrumented counterparts. Cold means fresh process/no index with OS cache not dropped; warm is open + 48-byte read + close after indexing. Production-path harness uses real `path.cpp`/`bink_media.cpp`, no mod overlays, and empty save selection only because game-archive paths do not consult saves. Full and focused timings are not pooled.
- Raw result: median (p95). Full cold 13.340700 (14.148600) ms normal, 14.441200 (15.172000) `-pg`; full warm 17.627550 (18.011500) µs normal, 17.747850 (19.790700) `-pg`. Production-path cold 18.536700 (20.400800) normal, 19.445050 (21.458900) `-pg`; warm 12.519750 (12.863000) normal, 12.913600 (13.312400) `-pg`. Each has 50 cold and 10×20,000 warm runs per build. Deep attribution adds 500 cold and 2,000,000 production-path warm lookups.
- Flat attribution only: cold 194 samples led by hashtable find 41.24% and Bink `Normalize` 14.95%; production-path warm 202 samples include Bink `Normalize` 16.98%, `HostPath` 13.84%, hashtable find 11.95%, `OpenFileSource` 5.03%, path `Normalize` 3.77%, `GuestCurrentDir` 1.26%. Startup/teardown/harness work is excluded from interpretation; these are not wall-time fractions.
- Correctness: unmodified UBSan Bink suites pass at 32,000/44,100 Hz. All cohorts match cold `852fa1ad3927b57d` and 20,000-iteration warm `4c6fe5c0653cf6bd`, including cross-harness checks. Missing-zlib and include-path failures are preserved. Optimized gprof call arcs are invalid even non-PIE (unreachable `CheckPublic`/`MemorySource::Read`, repeated `BuildIndex`), so no arc counts or ms/call are used; standalone `HostPath` results are not production attribution.
- Decision: **inconclusive for optimization; cold/warm host baselines and candidate areas recorded**. No cache, normalization, path, archive, or Bink change is adopted/rejected. No cold-storage, phone loading, FPS, or actual-game share claim.

## P3-BINK-DECODE — Corrected full-sample decode and registered-plane profiles

- Hypothesis: memory-backed sequential decode can isolate media CPU work from archive/storage timing, while a separate registered-path cohort can include production plane uploads with decoded-output verification.
- Baseline/candidate: production Bink at `a5c9c22e97fccc6e44c5cceb50dbb4a462e234bc`; no optimization candidate. Evidence-only harnesses time `BinkDoFrame`, export/hash YV12 outside the timer, and optionally register two plane sets.
- Exact files: valid `bink_decode_fixed{,_registered}_profile.cpp`, `test-only-decode-fixed*.diff`, `bink-decode-fixed{,_registered}-normal`, `decode-fixed{,_registered}-normal.log`, `decode-fixed-summary.{txt,json}`, commands/hashes under `profiling/rcf-bink-gprof/`. Invalid original and diagnostic sources/binaries/logs are separately retained.
- Environment/settings/scene/cache: x86_64 host, normal `-O2`, pinned static FFmpeg n7.1.1 without x86asm and zlib v1.3.1; real 15,563,652-byte `valve.bik`, 291 frames at 1920×1080. Twenty fresh processes per path; warm memory input, no storage/render/audio-device/real-time playback. Registered path uses two populated sets with padded strides and negative pitches. Export, hashing, input checks, and plane comparisons are outside decode timing.
- Raw result: unregistered medians across per-run summaries: open 1.447354 ms, decode total 545.069728 ms, mean 1.873092 ms/frame, median 1.117999, per-run p95 4.490041, p99 5.052517, max 5.452108 ms. Registered: open 1.403390, total 677.875458, mean 2.329469, median 1.576090, p95 4.952626, p99 5.538735, max 6.006153 ms. These are not pooled frame percentiles or an optimization A/B.
- Correctness: each path passed 20/20 processes and 5,820 frames with common digest `d68445b7a59b9b4a`; input was unchanged after every frame. Registered Y/Cr/Cb in both sets matched exported YV12 after every frame. An earlier 20-run cohort is invalid because `Call()`'s `0x400000` guest stack overlapped the input from `0x100000`; a diagnostic found a frame-1 change at offset 3,145,712. Its `eaf346552382e543` digest/timings and the resulting registered frame-73 failure are excluded as production evidence. Corrected harnesses allocate a dedicated 64-KiB stack first.
- Decision: **valid host baselines; inconclusive for optimization**. The registered/unregistered difference is descriptive, not pure upload-cost isolation. No production change adopted/rejected and no Android/playback/FPS conclusion.

## P3-TOOLS — Measurement and comparability-guard validation

- Hypothesis: existing counters and the evidence parser can be validated for logical correctness and can explicitly reject historical logs that lack controlled per-phase metrics.
- Baseline/candidate: source `a5c9c22e97fccc6e44c5cceb50dbb4a462e234bc`; existing implementations/tests only, no candidate or runtime change.
- Exact files: `present_stats.cpp/.h`, `present_stats_test.cpp`, `diagnostics_test.cpp`, `perf_hint_test.cpp`, `tools/perf_evidence.py`, `tools/test_perf_evidence.py`; raw outputs under `profiling/instrumentation-validation/`.
- Environment/settings/scene/cache: x86_64 host, CI-style `-O1 -g` UBSan builds and `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`; synthetic counter/parser fixtures plus the revision-`51e51` historical native/journal logs. No scene, cache, or device benchmark.
- Raw result: Present baseline/ADPF count/FPS/percentile/detailed-total tests pass; diagnostics opt-in/window/CPU/worst-event/`/proc` tests pass; performance-hint tests pass; parser fixtures, real-log coverage, mixed-revision rejection, and A/B comparability guards pass. Historical summary has 61 Present-wall records and 20 tested phases; labeled self-compare is `comparable=false` with `no common tested phases with performance metrics`.
- Correctness: all executables/tests exited zero. The final summary initially asserted 60 Present-wall records and failed; actual output is 61, preserved in `summary-initial-failure.log`. Parser `present_windows` means Present API wall duration, not Presented-frame intervals; current per-phase frame metrics are separate.
- Decision: **instrumentation logic validated for covered cases; no optimization decision**. No hardware timing accuracy, visual correctness, device FPS, or historical A/B claim.
