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

## P4-DISPATCH — Production-scale split lookup storage

- Hypothesis: parallel 32-bit key and function-pointer arrays reduce probe/allocation footprint without changing dispatch semantics; an indexed-slot variant may reduce memory further but can alter hot lookup costs.
- Baseline/candidate: `f1737b23a6c4fcaf9255b0f4f4cd765ea2f5a704`; original/split/indexed evidence sources and diffs under `phase-04/dispatch/`. Split was then applied to uncommitted `src/native/runtime.cpp` (checkpoint SHA-256 `e4c117dc40c369e05b99071a85cdbd7a37b438303b59ad23cfe008e5dc540299`); indexed slots were not applied.
- Exact files/environment/settings: actual `runtime.cpp`/`imports.cpp`, all 126,974 real table associations, generated checked stubs, x86-64 GCC 12.2.0; normal `-O2`, UBSan, TSan, and eight-thread first-use. No game scene or graphics cache. Cold fresh processes and warm full-table verification are separate; direct-shell cold RSS, order-balanced 2,000,000-call A/B, and independent 5,000,000-call confirmation, seven batches each.
- Raw result: original/split cold medians 2.315824/1.778324 ms and maxRSS 11,668/10,472 KiB. Confirmation original/split ns/call: lookup 14.193864/12.466144, repeat 2.665343/2.459777, miss 25.560324/15.328398, dispatch 22.684933/21.398173; paired ratios 0.875280/0.919683/0.601714/0.937314, dispatch paired p95 1.002434. Initial indexed repeat regressed (candidate/original paired ratio 1.052112), with variable other gains. Full raw tables and failures are in `dispatch/ab-summary.*`, `confirm-summary.*`, and per-process logs.
- Correctness: all final layouts pass 126,974 lookups/dispatches, misses, UBSan/TSan, and concurrent first-use. Original/split pass runtime bridge; actual post-patch split also passes guest memory, detour, plugin v1/v2, feature-policy, and production assembly-equivalence checks. Permanent five-entry same-hash fixture passes on original and split. Initial index target-zero and production conversion/fixture-path failures are preserved and excluded.
- Initial decision: **provisionally adopt split arrays for integration; reject parallel indexed slots**. This was superseded by P4-LOCALITY: split was later rejected for production and reverted. The standard-cohort chronology and synthetic host/memory evidence remain valid, but are not Android timing, FPS, frame pacing, or an actual target trace.

## P4-DIRECT — Flat address-indexed dispatch table

- Hypothesis: direct indexing could remove hash probes if the real entry addresses have a sufficiently dense aligned span.
- Baseline/candidate: exact address list from `f1737b23`; evidence-only distribution analysis, no runtime candidate built.
- Environment/settings/scene/cache: corrected first-column parser over 126,974 addresses; analytical memory/alignment model only, no scene/cache/timing.
- Raw result: addresses span `0x7c001005`–`0x7cd8ba20`, lack uniform alignment above one byte, and would need 113,594,592 pointer bytes at byte granularity versus 3,145,728 split-hash bytes. Invalid initial two-column token parse is preserved.
- Correctness/decision: count/uniqueness/range assertions pass after correction; **reject flat direct table** on memory/alignment grounds. No performance or device claim.

## P4-HASH-SIZE — 17/18/19-bit split-table sizing

- Hypothesis: a smaller table could save memory, or a larger table could reduce probes enough to justify its storage/cold cost.
- Baseline/candidate: current split 18-bit source versus evidence-only 17/19-bit one-line size variants under `dispatch/hash-size/`; no production size change.
- Environment/settings/scene/cache: exact addresses and checked stubs; normal+UBSan full verification and eight-thread UBSan first-use for all sizes. Seventeen-bit received only a separate 10,000-call smoke after analytical rejection. Eighteen/nineteen received 24 direct-shell cold pairs and 12 alternating warm pairs/mode at 2,000,000 calls × seven batches. Python-launcher cold/RSS results are excluded; replacement cold data is `cold-shell.log`.
- Raw result: 17-bit mean success/miss probes 12.873683/446.796510 (maxima 3,601/3,648). Nineteen-bit versus 18-bit cold medians 3.212444/2.139620 ms, paired ratio 1.525857; maxRSS 13,672/10,476 KiB. Nineteen/eighteen warm paired ratios: lookup 0.931419, repeat 1.010304, miss 0.753250, dispatch 0.966940, with retained ranges.
- Correctness/decision: all built variants pass the stated checks and warm digests match. **Reject 17-bit; do not adopt 19-bit** because the memory/cold/repeat tradeoff lacks a real target-mix or device justification. No FPS inference.

## P4-STATIC — Static-BSS versus heap split storage

- Hypothesis: same-size static storage could remove allocation/pointer overhead without changing probes.
- Baseline/candidate: current heap split `runtime.cpp` versus evidence-only `runtime_static18.cpp`; no production static-BSS change.
- Environment/settings/scene/cache: exact-address normal/UBSan/TSan verification and eight-thread first-use; 24 direct-shell cold pairs and 12 alternating warm pairs/mode at 2,000,000 calls × seven batches; x86-64 ELF section inspection. No Android loader or scene run.
- Raw result: static/heap cold paired median ratio 0.982646 (p95 1.058392), maxRSS medians 10,468/10,476 KiB. Warm static/heap ratios: lookup 0.980445, repeat 1.017245, miss 0.970544, dispatch 0.976700, with variability. Static adds 3,145,760 NOBITS bytes to the host benchmark ELF.
- Correctness/decision: all stated checks and digests pass. **Inconclusive/not adopted**: small variable gains, repeated-target regression, and unknown Android loader behavior do not justify replacing heap allocation.

## P4-HASH-CONST — Multiplicative hash-constant screen

- Hypothesis: a same-cost constant might improve exact-table probe distributions without extra instructions or memory.
- Baseline/candidate: current `0x9e3779b1` versus seven established odd multipliers; analytical evidence only in `dispatch/hash-function-model.json`, no production change or timing candidate.
- Environment/settings/scene/cache: exact insertion order, successful probes, and four guaranteed-outside miss sequences; 18-bit/262,144-slot model. No scene/cache/timing.
- Raw result: current mean success 1.343645 and mean across four miss models 2.251567. Alternatives trade small successful/miss changes in different directions; none gives a robust meaningful win. For example `0x165667b1` has success 1.344716 and miss mean 2.172965, while `0x7feb352d` has success 1.326177 but worse xor-high-bit misses than current.
- Correctness/decision: all modeled sequences are verified outside the entry range; **inconclusive/no candidate adopted**, avoiding overfitting a synthetic sequence. No runtime or FPS claim.

## P4-LOCALITY — Split-layout localized target sensitivity and reversal

- Hypothesis: standard sequential/repeated/miss cohorts may not represent localized indirect targets; independent hot-set sampling should confirm or reverse provisional split adoption.
- Baseline/candidate: original `runtime.cpp` versus the temporarily integrated split variant; exact evidence sources/binaries under `dispatch/locality/` and `locality-confirm/`. Production was restored to original SHA-256 `f19dce078f4ee54f2c1cb831df47335474e64c44933edf6b51d53ebef2faadf1` after this experiment.
- Environment/settings/scene/cache: same x86-64 host, actual 126,974 addresses and checked stubs. Initial evenly spaced unique sets and independent uniform partial-Fisher-Yates shuffled sets; 16/256/4,096/16,384 targets, mixed one-miss-per-16 and localized dispatch. Twelve alternating pairs/case; initial 2,000,000 and confirmation 5,000,000 calls × seven batches. No actual scene/target trace.
- Raw result: initial/confirmation split-original paired medians: lookup16 1.048833/1.035959, lookup256 1.036827/1.034367, lookup4096 0.931069/0.959913, lookup16384 1.120699/1.157686; mixed16 1.022126/1.017227; mixed4096 0.982776/1.010203; dispatch16 0.992150/1.011768; dispatch4096 0.976991/0.936729. Later three/four-layout screenings reproduce directional localized costs with variability.
- Correctness: both layouts pass UBSan full verification and all initial locality smoke cases; every timed pair has matching digest. Independent verifier recomputes all medians/ratios from raw logs.
- Decision: **reject split for production and revert the provisional patch**. Full-dispatch gains in some modes do not resolve raw localized lookup regressions or unknown real target mix. No FPS/device inference.

## P4-LAYOUTS — Interleaved index8, packed12, and aligned buckets

- Hypothesis: keeping key/function information closer could remove split's localized cost while retaining a smaller table.
- Baseline/candidate: original and split versus evidence-only 8-byte interleaved key/index, packed 12-byte key/function, and aligned two/four-entry buckets under `dispatch/index8/`, `packed12/`, and `buckets/`; none was applied to production.
- Environment/settings/scene/cache: same exact-address standard plus shuffled-locality harness; 12 rotated/reversed processes per layout/case, 2,000,000 calls × seven batches. Normal full verification for all, UBSan/TSan/concurrency as recorded per candidate. No scene/device cache.
- Raw result: index8 ratios reach 1.808098 lookup16384 and 1.169088 dispatch4096. Packed12 ratios include 1.004018 sequential lookup, 1.027609 lookup16, 1.031651 lookup16384, and 1.031758 dispatch4096. Bucket2/bucket4 repeat ratios are 1.0676/1.0996 and lookup16384 ratios 1.0705/1.0521; neither improves standard misses.
- Correctness: every timed trio/quartet has matching digest; stated normal/UBSan/TSan/concurrent checks pass. Initial bucket cast failure is preserved before valid measurement.
- Decision: **reject all three alternatives and stop the layout search**. No robust workload-independent win or Android unaligned/indexing tradeoff is established.

## P4-GENERATED — Generated dispatch/TSO source and assembly audit

- Hypothesis: static generated-code analysis can determine whether flat-direct guards or obvious residual ordering work remains before considering a semantics-sensitive candidate.
- Baseline/candidate: unchanged generated source at `f1737b23`; no optimization candidate. Evidence under `phase-04/generated/`.
- Exact files/environment: all 192 `part_*.c` files/126,974 functions; aggregate path/content hash and per-file census. `part_0005.c` compiled with isolated Zig/LLVM 22.1.8 for AArch64-musl, production-like flat memory/TSO flags. No execution or scene.
- Raw result: 280,274 direct guards have fallback dispatch eliminated under flat memory, leaving 23,575 residual sites (1,671 constant/21,904 computed). Source has 579,100 shared scalar and 301,179 plain-stack scalar access sites. Partition assembly has five residual dispatch call/branches matching source, no `RecompDirect`, and static 159 acquire/142 release/272 DMB instructions.
- Correctness/decision: source/census/assembly hashes and structural assertions pass. **No TSO/alignment relaxation adopted**: static slow-path instructions are not dynamic costs, and no target trace/Android timing supports a semantics change. Full native linking is unavailable because DXVK/FAudio/FFmpeg dependencies are absent.

## P4-VERIFY — Independent raw-cohort integrity check

- Hypothesis: an independent parser can validate process counts, work/digests, and all locality/alternative summary statistics before final disposition.
- Baseline/candidate: no runtime candidate; current production must equal original `f19dce078f4ee54f2c1cb831df47335474e64c44933edf6b51d53ebef2faadf1`.
- Exact files/environment: `phase-04/verify_phase04.py`, raw logs and summaries across all dispatch cohorts; no new timing or scene.
- Raw result: 2,778 timed processes verified: 90 initial cold, 288 initial warm, 288 confirmation warm, 1,824 locality/alternative, and 288 size/static cold/warm. All entry/iteration/batch/count/digest checks and independent locality/alternative median/ratio recomputations pass.
- Correctness/decision: **evidence integrity pass**; original runtime hash confirmed. This adds no performance or device claim.

## P4-PAIRED — Descriptive paired-sign and final regression audit

- Hypothesis: a per-repetition sign audit can distinguish repeated directional tradeoffs from isolated median ratios without adding another performance cohort or overstating statistical confidence.
- Baseline/candidate: existing raw rows only; split, index8, packed12, bucket2, and bucket4 versus original. `phase-04/paired_sign_audit.py` recomputes every stated median ratio and counts candidate-faster/slower pairs. No source or timing change.
- Raw result: all **100 case/candidate summaries** recomputed exactly. Split lookup16 is slower 12/12 in both locality protocols; lookup4096 is faster 11/12 and 10/12, showing a workload tradeoff. Index8 lookup16384 is slower 12/12 and dispatch4096 10/12 despite standard lookup/dispatch wins 12/12. Bucket2/bucket4 repeat and lookup16384 are each slower 11/12. Packed12 remains mixed.
- Correctness: final restored-source native/mod/script/graphics/policy regression passes; real-DLL mixer/biquad/ADPCM/skin differentials have zero mismatches. Permanent collision/concurrent-first-use fixture passes original and split under UBSan/TSan/O2, followed by 128/128 race-schedule stress repetitions in each of those six configurations (768 processes). Both layouts also pass an isolated Zig 0.17.0/LLVM x86-64/musl compile/run; its initial wrong-path invocation is preserved and this is not Android/Bionic equivalence. Both expected-failure controls are detected. `verify_final.py` checks these results and named hashes; its initial wrong-log-path and later CI-hash-path failures are preserved and corrected.
- Decision: **analysis confirms the existing rejections; no new optimization disposition**. Sign counts are descriptive, not significance/confidence claims or Android/device evidence.

## P5-DXT32 — Direct DXT2-5 colour/alpha to BGRA rows

- Hypothesis: decoding DXT2-5 colour and alpha directly into destination rows removes the temporary 64-byte tile/per-pixel work implicated by the Phase 3 destination sensitivity, without changing palette or alpha arithmetic.
- Baseline/candidate: exact `a5d4d2436bbf77f23a333abe9992ae3934464200` baseline source SHA `39dc85ff388bc405c839d300554687ac6cf48dc5a378c7d60fe20384b585e80c`; initial evidence-only direct32 SHA `62d17bea4e4eaabf26f2d2e0827d825137100cf51aee6565999dd1b30f13787e`, plus row-store and noinline controls. Production integration was deferred until the controlled checks.
- Exact files/environment/settings: `d3d9_dxt.cpp` candidates/diffs, existing and working-set harnesses under `phase-05/`; x86-64 GCC, `-O3 -DNDEBUG`, two logical CPUs. Synthetic deterministic DXT1–5 inputs, seven timed batches, rotated/interleaved processes. Size/destination-slot configurations are explicit in `working-summary.json`; no game scene, phone cache, or FPS measurement.
- Raw result: initial direct32/baseline paired medians 0.714322 DXT3 and 0.733313 DXT5 BGRA. Across 64/256/512/1024 and 512 destination slots 1/4/16/32, the 252-process sweep gives DXT3 ratios 0.707137–0.738566 and DXT5 ratios 0.705982–0.763319 by configuration. Packed16 medians are near unity with wide ranges; DXT1 controls vary. Noinline is generally worse for DXT3 and mixed elsewhere. Full raw rows/logs and independently recomputed summaries are preserved.
- Correctness: production graphics UBSan and benchmark digests pass. Each candidate passes 9,480 full differential cases/19,214,680 compared bytes in normal and ASan+UBSan builds, including guards/padding/offsets/negative pitches. The working cohort has 252 process logs, 1,512 rows, 504 digest trios, and 252 independently recomputed summary groups. The outer wrapper's status-1 anomaly after complete output is preserved and separately assessed.
- Same-process confirmation: 24 fresh combined-binary processes at 512x512, 1/16 slots, 14 alternating batches ×100 iterations produced 2,016 output-equal batch pairs. Direct32 ratios are DXT3 0.715007/0.722852 and DXT5 0.732585/0.750275; packed16 is near unity or slightly faster. DXT1-32 is 1.007792/1.009869 and DXT1-16 0.957879/0.968701, so unchanged-path code-layout effects are explicitly retained in the decision. The initial even-median smoke error was corrected before measurement.
- Layout/target/final integration: late noinline is rejected (DXT1-32 1.029062/1.028423 and slower DXT3); lazy alpha is correct but rejected for slower DXT3/5 (0.751843/0.763256 and 0.754922/0.772249). Original inline remains selected. AArch64-musl static output grows from 1,207 to 1,684 instructions and 320 to 368 stack bytes; this is a limitation, not target timing. Intermediate direct32-only production source SHA is `a14f0e58bca272867a35c61a2693f08253341766c4cce1e3cc3396bb0ca233a5`; its comment-only change from `62d17bea…` leaves `.text` SHA `44e64ad42f18831f6e504b27faf2525e1f3466a26ff298fa32b57b6133509033` unchanged. Exact-source normal/ASan+UBSan differential and the 65.074660-second broad production-source regression passed; all four benchmark digests match baseline.
- Decision: **direct32 algorithm adopted after host validation and retained in the final combined source**. Controlled evidence supports lower host DXT3/DXT5 BGRA decode time, not Android/FPS/frame-pacing improvement. Full native linking is dependency-blocked and device validation remains absent. The subsequent palette increment is recorded separately below.

## P5-DXT16 — Direct DXT2-5 to packed16 rows

- Hypothesis: precomputing four A1R5G5B5 palette colours and applying the same per-pixel alpha threshold could remove the remaining generic intermediate tile for packed16 without disturbing direct32.
- Baseline/candidate: direct32 SHA `a14f0e58…` as the incremental baseline; evidence-only `candidate-direct-all.cpp`, `candidate-direct-all-n16.cpp`, `candidate-direct32-n16.cpp`, and `candidate-direct-explicit.cpp`. Exact hashes and diffs are under `phase-05/direct16-candidate-hashes.txt`; no form entered production.
- Exact files/environment: same x86-64 GCC `-O3 -DNDEBUG` dual harness, 64x64, 20 iterations and two alternating batches per smoke. Direct-all also ran the complete normal/ASan+UBSan differential. These are screening smokes, not long-cohort or device results.
- Raw result: all four forms materially regressed BGRA versus direct32. DXT3/DXT5 ratios are 1.682664/1.474472 (both inline), 1.631414/1.376156 (direct-all noinline16), 1.608820/1.605351 (minimal noinline16), and 1.673543/1.804560 (explicit BGRA/generic fallback). Packed16 was often faster, but not enough to offset that trade.
- Correctness: direct-all passed 9,480 cases/19,214,680 compared bytes normally and under ASan+UBSan; every dual smoke verified complete output equality.
- Decision: **rejected**. Long timing was not spent on repeatedly bad integrations. Production remained direct32-only while this line was evaluated.

## P5-PALETTE — Remove invariant four-colour palette alpha interpolation

- Hypothesis: in the four-colour palette path, both endpoints and both interpolated colours have alpha 255; computing only RGB and explicitly storing alpha 255 removes redundant work without changing any output byte.
- Baseline/candidate: incremental baseline direct32 SHA `a14f0e58bca272867a35c61a2693f08253341766c4cce1e3cc3396bb0ca233a5`; candidate/final SHA `40c092c82ad0da0d606ea1da8c8a4bfbe041526b7b5e3b2bb16f2b1b0f038b1f` in `candidate-palette-alpha.cpp`, then exact-copied to `src/native/d3d9_dxt.cpp`.
- Exact files/environment/settings: `candidate-palette-alpha.diff`, `dual-palette-alpha-o3`, and `run_dual_palette.py`; x86-64 GCC `-O3 -DNDEBUG`, 512x512, 1/16 destination slots, 24 processes, 14 alternating batches ×100 iterations, deterministic input. No scene, phone, quality change, or FPS measurement.
- Raw result: candidate/direct32 paired medians at 1/16 slots are DXT1 BGRA 1.014120/1.013846; DXT1 packed16 0.982783/0.990637; DXT3 BGRA 0.994153/1.002547; DXT3 packed16 0.921225/0.933343; DXT5 BGRA 0.986819/0.979198; DXT5 packed16 0.957948/0.963863. The DXT1-32 regression is explicitly included in the tradeoff. A further 42-process/3,528-batch-pair sweep across 64–1024 sizes and 1–32 slots gives DXT3 packed16 0.918043–0.936652 and DXT5 packed16 0.889590–0.963885 in every configuration; BGRA is near unity, with DXT3 1.011548 at 32 slots. All 42 logs and 42 summary groups independently recompute.
- Correctness: 2,016 fully output-equal batch pairs; 9,480 differential cases/19,214,680 compared bytes pass normally and under ASan+UBSan. AArch64 stays at 1,684 instructions/368 stack bytes with only two store-order swaps. Final actual-source broad regression passes in 64.301764 seconds and all four benchmark digests match 0.15. Independent verification covers 96 dual logs/8,064 batch pairs across four full variants.
- Decision: **adopted in the final Phase 5 source**. It preserves the direct32 BGRA gain and improves packed16 host decode time; no Android/Bionic execution, FPS, frame-pacing, or sustained-device claim.
