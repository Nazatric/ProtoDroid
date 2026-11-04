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

## P6-NOTIFY-MODEL — Bounded wakeups and mixed-waiter progress

- Hypothesis: limiting auto-reset-event/semaphore notifications could reduce wake herds, but only if every multi-object waiter is handled conservatively. A wake-one policy or a guard for only `all=true` may lose progress.
- Baseline/candidate: merged `00976d939e0cba01af908a8aecd4a02db6d41066`; exact baseline `win.h`/`kernel32.cpp` versus evidence-only naive, all-guard, and multi-guard variants. No production file changed. Candidate/source hashes are in `phase-06/candidate-hashes.txt` and `model-build-hashes.txt`.
- Environment/settings/scene/cache: two-logical-CPU x86-64 host, GCC C++17 `-O2 -DNDEBUG`, actual variant `win.h` plus a line-mirrored `WaitKernel`/Event/Semaphore model. Deterministic synchronization scenes, no game, phone, shader cache, FPS, or quality setting.
- Raw results: `phase-06/semantics-{baseline,naive,all-guard,multi-guard}.jsonl`; all four harnesses completed with zero expectation failures. Naive lost single-waiter progress in both event and semaphore all/any mixed scenarios; all-guard fixed all=true but still lost both multi-any scenarios; baseline and multi-guard preserved progress in every scenario. The reverse-registration control progressed in all variants. Back-to-back semaphore releases, manual/auto events, compound all acquisition, and finite-timeout cleanup also matched expectations.
- Correctness/decision at this incremental checkpoint: **reject naive and all-guard** on demonstrated lost progress. Multi-guard remained inconclusive pending timing; **P6-NOTIFY-TIMING below subsequently rejected it**, so no variant remains a candidate and no device claim is made.

## P6-NOTIFY-TIMING — Bounded notification throughput/latency tradeoff

- Hypothesis: the correctness-safe multi-object guard might reduce host wake cost enough to justify integration; a structure-only control would expose layout/TLS effects.
- Baseline/candidate: exact merged `00976d9` baseline, evidence-only structure-only control, and refined multi-guard (`Notify()` left unchanged for manual/thread paths; `NotifyLimited` only for auto/semaphore). No production edit.
- Environment/settings/scene/cache: same two-CPU host and GCC model, non-instrumented fresh processes, 16 rotated repetitions per policy/scenario, 480 total processes. Ping workloads release one permit and await one acquisition; batch workloads use a corrected per-round barrier so every worker consumes at most one permit per cohort. No game scene, cache, FPS, or device.
- Raw result: `phase-06/notify-cohorts.jsonl`, `notify-cohorts-summary.json`, and run/hash logs. Multi-guard/baseline median elapsed ratios were 2.231109 (four-worker event), 1.559205 (eight-worker event), 1.746289 (four-worker semaphore), and 1.453210 (eight-worker semaphore); all were slower in 16/16 pairs. Multi-guard/structure ratios remained 2.006187, 1.544121, 2.043458, and 1.481128. Four batch scenarios ranged from 0.986005 to 1.049498 versus baseline and 0.983988 to 1.021123 versus structure, with wide ranges and no directional gain. Single-worker results were near unity with wide variance.
- Correctness/decision: all 480 runs completed their exact operation counts with zero timeouts and correct final state. **Reject multi-guard and the entire bounded-notification line for 0.16**: the large single-release latency regressions are consistent, and batch workloads do not establish an offsetting benefit. No source integration or phone inference.

## P6-PRESENT-BOOKKEEPING — Disabled-diagnostics accounting cost

- Hypothesis: per-Present sample retention and once-per-second p99 selection might justify a scheduling/bookkeeping change, especially at unlocked menu rates.
- Baseline/candidate: actual `00976d9` `present_stats.h`/`present_stats.cpp`; no candidate because the baseline measurement would determine whether one was warranted.
- Environment/settings/scene/cache: two-CPU host, GCC `-O2 -DNDEBUG`, actual tracker with synthetic monotonic intervals at nominal 60/240/1,000/4,000/10,000 Presents/s, detailed collection disabled, 12 fresh tracker repetitions and three complete windows each. Integer-window rounding is explicit (61/241 calls at 60/240). No game scene, GPU, device, or cache claim.
- Raw result: 550,872 timed calls in `phase-06/present-stats-{rate}.csv`, with `present-stats-summary.json` and hashes. Ordinary-call median was 25 ns at every rate. At 60 Hz, window-finish median/max were 127.5/418 ns. At 10,000 Hz they were 10,604/21,023 ns (10.604/21.023 µs); ordinary p99 was 49 ns, with unrelated host scheduling outliers retained in the raw samples. The initial 60 Hz rounding setup failure is preserved separately.
- Correctness/decision: all returned intervals, window frame counts, zero detailed totals, and 550,872 report counts passed; the existing present-stats regression also passed. **Reject a Present bookkeeping rewrite for this phase**: measured ordinary/window costs do not establish a meaningful frame-pacing bottleneck. No source change or phone FPS inference.

## P6-PRESENT-TID — Defer unused TID work with detailed collection off

- Hypothesis: avoiding the thread-local TID guard/access on the ordinary `RecordPresent` path could reduce bookkeeping without changing detailed samples.
- Baseline/candidate: exact `00976d9` `present_stats.cpp` (also an identical A/A control) versus evidence-only `candidate-present-tid.cpp`; no production edit. The candidate loads the detailed flag and returns `AddPresent(now, 0, false)` before TID initialization; detailed mode retains TID sampling.
- Environment/settings/scene/cache: actual adapter, two-CPU host, GCC `-O2 -DNDEBUG`, off/on modes, 24 rotated repetitions, 200,000 measured calls per process, 144 correct processes. First-repetition per-call raw arrays are retained for all six policy/mode combinations. No game scene, device, or cache.
- Raw result: off candidate/baseline total-time ratio 0.989265, candidate/control 0.972902, but A/A control/baseline was 1.012942 with ranges roughly 0.78–1.31. The median-total candidate/baseline difference was only 96,747.5 ns over 200,000 calls (~0.48 ns/call); median per-call medians were 59/58 ns. On-mode candidate/baseline was 1.003151. Raw `present-tid-cohorts.jsonl`, summary, six binary sample arrays, and hashes are preserved.
- Correctness/decision: every run had exact Presented counts and returned intervals, zero off-mode detailed samples, and correct on-mode sample counts/TID. **Reject/not adopted**: the small apparent off-path gain is within A/A variability and immaterial in absolute cost, while detailed mode is neutral. No FPS or device claim.

## P6-ADPF-ATTRIBUTION — Present-thread session registration lead

- Hypothesis: explicitly adding the actual Present thread to the ADPF thread group might improve sustained CPU scheduling if that long-lived thread is not already represented.
- Exact inputs: merged `00976d9` `perf_hint.cpp`, `perf_hint_session.h`, `d3d9.cpp`, `main.cpp`, and `xaudio2.cpp`; supplied historical `proto-native.log` SHA-256 `35af134357452e1841e5ed9508035edd9e60cc54b0df3d7da6bf542667e858d1`; NDK-derived API header at `P-404/android_frameworks_native` `c6a090fd350b6a8de7dc3ce51aaa1bc8d5493b9b`. No production candidate was created.
- Environment/settings/scene/cache: read-only historical diagnostic-tour log, not an exact-0.15 or 0.16 measurement; source/API inspection only. No phone, new scene, cache state, FPS comparison, or thermal result.
- Raw observations: 61 `diag: present worst` lines contain 241 retained TID entries after excluding the outer diagnostics logger; 240 name TID 5192 and one names 5147. TID 5192 requested priority 0. The same log reports 3,130 ADPF reports and zero report/target errors in its final window, but no session TID roster or thread lifetime. `phase-06/historical-log-wait-audit.json` preserves counts, source lines, and hashes.
- API/source findings: `PerfHintStart` runs after `EngineInitialize`; priority 2/15 and FAudio callback paths register threads, while priority 0 does not. Dynamic `setThreads` is API 34 and replaces the group. The NDK header describes an inter-related, long-lived thread group and does not require the reporting caller itself to be a member.
- Correctness/disposition: **inconclusive, no patch**. There is no proof that TID 5192 was absent, remained alive for the whole session, or would benefit from registration. Adding short-lived threads or replacing the roster incorrectly could regress scheduling. Requires explicit roster/lifetime instrumentation and a physical sustained A/B; not a host benchmark candidate.

## P7-RCF — Defer basename construction on exact RCF hits

- Hypothesis: constructing `base` only after exact lookup misses could remove short-string work from ordinary exact RCF opens while preserving all fallbacks.
- Baseline/candidate: Phase 7 baseline `69f888334a474aa36db7f8cece0d68d78e6bcf0c`; exact `bink_media.cpp` SHA `40497b69…` versus evidence-only candidate SHA `4d985649…`, preserved with binaries/diffs outside Git. Production was restored after the experiment.
- Environment/settings/scene/cache: x86-64 GCC `-O2 -DNDEBUG`, actual `bink_media.cpp` and `path.cpp`, pinned zlib 1.3.1, 18,044-entry generated RCF, fresh processes, rotated baseline/identical-control/candidate order. Full-path runs include loose-file/mod-path policy around the archive resolver; direct runs isolate archive lookup. No physical storage, game scene, movie decode, or frame workload.
- Raw result: full path completed 108 processes/2,160,000 exact-output opens with medians 12,008.622/12,012.6645/11,951.1335 ns; candidate/baseline 0.995213 but paired median +2.8035 ns and 18/18 direction split. Direct archive completed 108 processes/10,800,000 opens with medians 2,987.5725/2,987.121/2,976.3615 ns; candidate paired median −5.0785 ns versus A/A −7.926 ns. Cold medians do not improve. Raw JSONL/summary/paired files and hashes are retained.
- Correctness: every timed open returned the expected digest. Baseline/candidate semantic outputs are identical at 8/8 checks covering normalization, basename, exact/basename RZ, loose priority/payload, and missing content. Independent raw verification passes 30/30 checks.
- Decision: **reject**. The candidate does not separate from the identical control, absolute cost is small, and no real open frequency or frame-loop share justifies retaining it. No runtime change, startup/streaming/FPS/pacing, or Android claim.

## P7-WAIT — Inline small multi-wait array and single-sized heap fallback

- Hypothesis: `WaitForMultipleObjects` spends avoidable time growing a vector for ordinary small counts; eight inline pointers and a sized larger fallback can remove that work without changing wait behavior.
- Baseline/candidate: baseline `69f8883` `kernel32.cpp` SHA `9893943e…` versus candidate/final SHA `5a358ee2…`, committed as `2e9e31bb30f1158238865800471c675f57fac5cb`. Only `src/native/kernel32.cpp` changed.
- Environment/settings/scene/cache: actual source with only import registration removed in the focused build; production flat-memory/guest-base/signed-char flags, x86-64 GCC `-O2 -DNDEBUG`, 16 rotated fresh-process rounds, 200,000 pre-signaled manual-event calls per process, 14 any/all cases from 1–64 handles. No game scene, phone, cache, or frame workload.
- Raw result: 672 processes/134.4 million calls passed with identical per-case digests. Candidate/baseline medians range 0.361003–0.779293; candidate is faster in 16/16 pairs in 13 cases and 15/16 at 64-handle all. Full baseline/A-A/candidate medians and paired data are in `phase-07/wait-ab-*` and the Phase 7 report. Independent different-order confirmation adds 1,344 processes/268.8 million calls, ratios 0.362502–0.767116 and at least 31/32 paired wins per case; combined total 2,016 processes/403.2 million calls. Allocator instrumentation independently shows zero candidate allocations through eight handles and one above, versus up to seven baseline growth allocations. Read-only original-DLL disassembly independently identifies one constant two-handle any/infinite call site and two dynamic-count any/infinite sites; static frequency remains unknown.
- Correctness: normal/A-A/candidate 98-check differentials match; extended normal+ASan/UBSan+TSan stress passes 192 processes, 24,960 checks and 18,432 wakes with zero sanitizer stderr. Independent timing verification passes 257/257. Broad regression passes 12/12 commands in 8.691055816s. Exact-code CI is pending at this ledger checkpoint.
- Decision: **adopt as host-validated candidate**, pending final CI/phase gates. No Android/Bionic timing, real call-rate, FPS, frame-pacing, or sustained-device claim.

## P7-RCF-MEMORY — Cold index duplication and process RSS audit

- Hypothesis: duplicated exact/basename `ArchiveEntry` strings might warrant deduplication, but only if footprint evidence justifies the lifetime/priority work.
- Baseline/candidate: unchanged baseline RCF resolver; no candidate. Source review found independent exact/basename entries and process-lifetime maps.
- Environment/raw result: 24 direct-launch fresh processes, 18,044 entries/1,118,858-byte generated archive, valid even warm-open count and expected digest. Cold median 18.7939795 ms; whole-process maxRSS median 18,620 KiB. Raw per-child wait4 data and rejected `/usr/bin/time`/odd-count failures are preserved.
- Correctness/decision: independent count/digest/stat recomputation passes. **No patch** because this is not index-only or Android pressure evidence, and pointer dedup requires independent duplicate-priority/lifetime handling. No memory-savings or startup claim.

## CONTINUITY-2026-11-04-0540 — Branch and evidence-store recovery

- Observation: a fresh clone initially reported `00976d9` plus uncommitted later content, and the external evidence directory was absent. This was an environment/state issue, not a performance experiment.
- Recovery: every reported file matched pushed `747db65f753ee01945b6c3d7e4aab51e96b8b23c`; the existing active branch was reattached without changing content or using another branch. Backup patch/document hashes are in `/home/user/protodroid-recovery/`.
- Evidence disposition: older external raw files/manifests are marked historical/unavailable. No result is fabricated or claimed as reopened. Exact-code Phase 7 CI `41773816427` independently confirms six successful build/test jobs and skipped release at `2e9e31b`.
- Decision at that checkpoint: Phase 7 remained active pending regenerated essential raw evidence and final gates; those gates subsequently passed as recorded below. No new runtime change or performance result is associated with recovery.

## P7-RECOVERY-VERIFY — Regenerated essential raw evidence

- Hypothesis: exact Git sources can re-establish the essential adopted-candidate and RCF correctness evidence after the external store loss, without presenting historical files as recovered.
- Baseline/candidate: exact parent of `2e9e31b` versus its `kernel32.cpp`; current `bink_media.cpp` unchanged from `69f8883`. Fresh sources/binaries/scripts are hashed in the new Phase 7 store.
- Raw result: fresh wait cohort 672 processes/134.4 million calls, candidate/baseline ratios 0.323330–0.773926 and 16/16 paired wins in all 14 cases; n=2/any 0.458088. Fresh stress 192 processes/24,960 checks/18,432 wakes, zero sanitizer stderr. Fresh RCF normal+ASan/UBSan semantics each pass 8/8. No fresh RCF performance experiment.
- Correctness: 257/257 timing verification, exact allocation counts, original-DLL caller audit, 12/12 broad regression in 7.652395760s, and exact-code CI six-job success all pass. Older 403.2-million-call and RCF timing raw data remain historical/unavailable, not merged into this fresh cohort.
- Decision: **recovery validation passes; no additional runtime change or device claim**. Phase 7 can proceed to final source-scope, release-preservation, documentation, and manifest gates.

## Phase 8 — D3D9 bridge and DXVK overhead

### H1: HostTable chunk acquire is redundant with entry acquire

- **Baseline/revision:** Phase 8 baseline `c033649`; exact baseline source and harnesses in `phase-08/`.
- **Candidate:** `ca25c4603ca3969fbaabd5e2dfc1264a50101925`; only the chunk-pointer load becomes relaxed. Entry acquire and all release stores/lifetimes remain.
- **Evidence:** exact production-class ARM64 `ldr` + `ldar` versus baseline `ldar` + `ldar`; 2,828,784-check normal stress and 707,196-check TSan stress per variant with matching digests; canonical ASan+UBSan/TSan stderr empty. Intentional process-lifetime LeakSanitizer finding is retained separately.
- **Timing:** fixed-order cohort rejected as confounded. Balanced 72-process x86-64 candidate/baseline median 1.006302, A/A 1.005189; neutral because x86-64 instructions are identical.
- **CI:** run `41781790100`, six build/test jobs successful, release skipped.
- **Native ARM64 evidence:** run `41791343265` / check `126445347530`: candidate/baseline 0.999767 (14/24), A/A 1.000522; median elapsed 29.8015065 ms versus 29.813208 ms over 20 million calls. The candidate timed loop retains its relaxed chunk `ldr` plus entry `ldar`, so it was not optimized away. Absolute median delta is only -0.000585075 ns/lookup. Raw confirmation is 1.000394 (11/24; A/A 1.000355), for a 48-pair combined 0.999926 (25/48; A/A 1.000417).
- **Disposition:** **rejected on materiality after initial structural adoption**. Final source restores the original chunk acquire at `9a65eb9b59dc1e755d434ae9204affef547ef5d4`; `ca25c46` and its passing CI remain historical evidence. No real-game frequency, FPS, or pacing claim.

### H4: resolve anisotropy before sampler calls

- **Candidate:** `f5be92170480d4d9a99924f41502108903e0449a`; resolve setting in `InitD3D9Bridge` after config/logging are ready and before guest calls. Defaults, clamp, and forwarded sampler policy unchanged.
- **Correctness:** exact production-block normal/TSan fixture passes 44 processes/176 checks across Android/desktop, explicit values, defaults, and concurrent reads. Broad regression 12/12. Independent candidate verification 70/70.
- **Performance:** rejected optimized-out same-TU benchmark retained. Canonical separate-TU balanced candidate/baseline 0.773923 (24/24); independent confirmation 0.784688 (36/36), with A/A 0.993443/0.997784. ARM64 loses the `ldarb` static guard. Combined x86-64 median is 0.779120 across 60 pairs (60/60), with 0.266–0.323 ns isolated setter savings. Initial native ARM64 is 0.375614 (24/24; A/A 1.000113), median 13.309993 ms versus 35.4287635 ms over 20 million calls, a 1.105938525 ns/setter median delta. Raw confirmation is 0.375510 (24/24; A/A 1.000018), for a 48-pair combined 0.375547 (48/48; A/A 1.000090) and 1.10613405 ns/setter final median delta.
- **Disposition:** **adopted**. Exact-code CI `41784574180` passed all six active jobs and skipped release. Isolated host function/ARM64 structure only; no phone FPS or real setter-frequency claim. Native ARM64 checks `126445347530` and `126454408467` pass exact source/protocol assertions; 144 final raw processes recompute successfully.

### H5: eager screenshot configuration

- **Evidence:** candidate removes two ARM64 `ldarb` guards. Initial default-path ratio 0.981703 (20/24), confirmation 0.991820 (22/36), with real `shot_now` polling unchanged. Exact normal+ASan/UBSan capture differential passes 16 cases/80 checks with identical PPM bytes.
- **Disposition:** **rejected on materiality**. Once-per-Present improvement is weak/inconsistent (combined 0.988630; confirmation absolute saving 0.230 ns/Present) and does not justify new global lifecycle state. Combined config candidate is unused; screenshot behavior is unchanged in source.

### DXVK audit

Existing state-cache and Mali compiler-serialization patches remain unchanged. Pinned source confirms sampler/render-state dedup after device-lock/StateBlock handling, protected-only recursive spinlocks, and bounded dedicated submission threads. Actual creation flags, redundant-call rates, and contention are unknown. No draw-state cache, compile-lock removal, dependency replacement, queue rewrite, or frame/render reduction is justified by the available evidence. Cross-target assembly uses external `ziglang 0.13.0`/Clang 18.1.6; it is not the production NDK or a device profiler. Exact NDK/ARM64 compilation is covered by CI.

### H6: native ARM64 exact-source CI fixtures

- **Revision:** measurement/workflow commit `fdd426729d53db0225194433283b3f57ef23134b`; run `41787828484` completed successfully; readable timing initially unavailable.
- **Final revision/files:** `9a65eb9b59dc1e755d434ae9204affef547ef5d4`; `src/native/d3d9.cpp` restores HostTable and retains anisotropy, `tools/measure_d3d9_bridge.py` builds fixtures, and `.github/workflows/build-apk.yml` executes/publishes them.
- **Device/build/settings/scene/cache:** native `ubuntu-24.04-arm`, AArch64, `g++ -O2 -std=c++20`; fresh process per timing row, 20 million calls, no game scene or shader cache involved. This is neither cold/warm game loading nor physical-device coverage.
- **Protocol:** exact HostTable `c033649`/pinned rejected `ca25c46` and anisotropy `ca25c46`/final `9a65eb9`, baseline/A-A/candidate, separate implementation objects, shared anisotropy main object, 24 balanced rounds, correctness checks, raw JSONL/artifact/Checks.
- **Current evidence:** final x86-64 tool validation HostTable 0.997411 neutral; anisotropy 0.815559, 24/24, A/A 1.002361. Earlier short/different-main tool outputs are preserved but excluded.
- **Current CI:** run `41787828484` passed all six active jobs but exposed no readable bytes. Run `41791343265` / check `126445347530` then supplied the initial ARM64 cohort: HostTable 0.999767 neutral and anisotropy 0.375614 (24/24). Its full run later completed successfully after the 403 cancellation response left it untouched.
- **Disposition:** initial ARM64 evidence collected; HostTable rejected and anisotropy retained. Raw final-source confirmation passes with 144 recomputed processes; final full CI `41794182100` passed all six active jobs and skipped release. Linux ARM64 is not an Android device or phone FPS/pacing measurement.

## Phase 9 — Shader compilation, loading, and cache behavior

### H1: avoid only unchanged shared state-cache backups

- **Hypothesis:** the exact 30-second ordinary-Play mirror currently recopies a populated cache even without source/target changes; a conservative identity tracker can eliminate only those redundant copies while preserving every changed-cache backup and external-wipe recovery.
- **Baseline/revisions/files:** production `9a65eb9b59dc1e755d434ae9204affef547ef5d4`, research `a2a4d4843143d6bc77482344f2479b187ac59754`; `src/native/state_cache.{cpp,h}` and `src/native/main.cpp`. Patched DXVK reference `c707d9026f33b6ab89639f154b6ac5f6326fa037` plus the full ordered Android patch series is audited in a separate external copy.
- **Device/build/settings/scene/cache:** source audit plus host exact-source filesystem fixtures; ordinary mirror period is exactly 30 seconds and normal execution is already asynchronous. No game scene, physical storage, shader compilation, or cold/warm device startup is measured. Cache remains enabled with existing DXVK v18/current-version handling and app-private authoritative file.
- **Safety/rollback:** preserve atomic replacement, full copies on any identity change or unstable source, last-good backup on invalid/missing source, external target recovery, and original behavior for untracked callers. Revert the tracker if differentials fail or timing is not material.
- **Candidate/implementation:** `55000220dad9de4e4d97f6759b7fbf9b716b0f28`; caller-owned tracking in `main.cpp`, heap-buffered complete-file comparison and source-probe reuse in `state_cache.{cpp,h}`, expanded tests, and exact-source benchmark/ARM64 Checks tooling. Metadata-only skipping was separately rejected after 8 failures in 3,304 stress checks; it is not part of the candidate.
- **Raw result:** final x86-64 unchanged candidate/baseline ratios are 0.060235/0.066586/0.102411/0.119319 at 4 KiB/64 KiB/1 MiB/4 MiB, all 12/12 wins. Native ARM64 check `126474212284` gives 0.075922/0.111711/0.085883/0.089859, all 12/12, with 85.55 µs/133.52 µs/2.326 ms/9.183 ms median savings per unchanged backup. Changed-source ARM64 ratios are 1.002254/1.010056/1.024110/0.999639 with mixed wins and near-one A/A; no changed-path performance claim. A later exact 179,336-byte real-cache host cohort gives unchanged ratio 0.06824111695 (12/12), 338.860 µs median saving, and changed ratio 1.03080881707 (5/7), +6.938 µs with A/A 0.979368; changed remains inconclusive. Final real-fixture ARM64 check `126488886424` gives unchanged 0.07396384637 (12/12), 443.623 µs saving, and changed 1.00273796317 (6/6), inconclusive. Raw JSONL is under `phase-09/final-tool-*`, `ci-h1-arm64-*`, `ci-h1-arm64-check-summary.md`, and `real-cache/host-*`.
- **Correctness:** expanded UBSan tests plus normal 16,504-check, ASan+UBSan 4,952-check, and TSan 2,476-check concurrent cohorts pass with zero failures/empty diagnostics. Identity collisions, same-size edits, append, target edit/wipe, path changes, retry, invalid-source preservation, and untracked compatibility are covered. Independent ARM64 recomputation passes 404/404. A linked syscall fixture proves unchanged skip performs zero writes/temp files/renames and changed copy adds three stats while retaining writes/rename. The final test-only read-only-directory fixture also passes local UBSan/ASan+UBSan/TSan, preserving targets/sources and retrying correctly; the final ARM64 UBSan fixture passes with empty stderr in check `126488886424`.
- **Disposition:** **adopted** for eliminating proven redundant asynchronous backup writes with exact-byte safety. No Android shared-FUSE, physical-device FPS/frame-time/pacing, shader-stall, or sustained-thermal claim.

### H2: direct SHA-1 padding for shader/cache keys

- **Hypothesis:** replacing dozens of one-byte `SHA1Update` calls in DXVK `SHA1Pad` with direct one-/two-block padding could reduce shader-key/cache-integrity CPU overhead without changing any digest or cache format.
- **Baseline/candidate/files:** DXVK 2.5.3 `c707d9026f33b6ab89639f154b6ac5f6326fa037` plus the existing ordered Android patches; candidate introduced at `a4c027de773678188ff1024ae4ffcdb522fcc8dc`, harness-fixed at `65fda1a9c1db348542d795607dd09cfd1e5d9b5e`. Only `src/util/sha1/sha1.c` changed in the DXVK candidate; `dxvk-zzzzzzzzzzzzzz-sha1-pad.patch`, `sha1_pad_test.cpp`, and `measure_sha1_pad.py` were dedicated test scaffolding.
- **Device/build/settings/scene/cache:** x86-64 GCC host and native `ubuntu-24.04-arm` AArch64 CI, separate baseline/A-A/candidate processes, balanced 12/16-round cohorts, deterministic payload sizes 20–4,096 bytes and source-derived v18 cache-entry sizes. No Android device, game scene, live shader-creation frequency trace, cold/warm device startup, GPU compilation, or frame-time measurement. The initial timing used synthetic/source-derived sizes; a later read-only audit found and validated the tracked real Prototype cache, recorded separately below.
- **Raw result:** native ARM64 check `126482355374` gives candidate/baseline medians 0.469202/0.796846/0.474083/0.516502/0.603472/0.707933/0.886047/0.967808 at 20/48/56/64/128/256/1024/4096 bytes, all 12/12 wins, with A/A 0.999158–1.002544. Absolute savings are respectively 125.846/28.355/218.233/191.348/189.769/190.972/192.017/184.220 ns per hash. Host exact-entry-size savings vary with `size mod 64`; raw host/ARM64 JSONL and compact rows are under `phase-09/sha1/` and `phase-09/ci-h2-arm64-*`.
- **Correctness/evidence integrity:** baseline/candidate output matches exactly for 12,128 records/1,572,405 bytes, including FIPS/million-`a`, chunking, and full contexts. Candidate ASan+UBSan and patched ARM64 correctness pass; an additional 384 high/wrapping-count full-context comparisons pass for both implementations. Independent check recomputation passes 873/873. Failed timing runs `41801355490` and `41802404581` are preserved: the former had no diagnostics, and the latter identified the benchmark-only missing-`<cstdint>` include-order bug through failed check `126481501323`; neither is used as performance evidence.
- **Disposition:** **rejected for 0.16 on absolute materiality** despite correct, faster isolated code. The later real cache contains 1,014 boundary/SHA-1-valid entries; applying the largest ARM64 saving to every entry bounds a full cache-load hash pass at only 0.221288058186 ms. Actual 134–174-byte entries were not separately retimed, and no meaningful loading or pacing impact is established. The production patch and H2 CI scaffolding are removed from final source; retired exact files and all success/failure evidence remain external. No shader-stall/FPS claim.

### Real-cache inventory and validation tooling

- **Hypothesis/purpose:** determine whether the tracked Prototype cache supplies real entry-count/size evidence and a workload-matched H1 fixture; this is evidence tooling, not a new runtime optimization.
- **Exact inputs/files:** current-baseline `prototypeenginef.dll.dxvk-cache`, 179,336 bytes, SHA-256 `759f38aaa80f86e4bf283e52a51f4ce3316e6609a57b4cac841c5592518930b1`; matching D3D9/native logs; new read-only `tools/inspect_dxvk_state_cache.py` and optional fixture support in `tools/measure_state_cache_backup.py`.
- **Environment/settings/scene/cache:** Python stdlib SHA-1/boundary validation plus exact-source x86-64 and native-ARM64 file-I/O fixtures. Originals remain read-only; every timed process uses a temporary copy. No new game scene, physical device, Android FUSE, GPU compilation, or frame-time result.
- **Raw result:** 1,014 boundary/SHA-1-valid monolithic VS+FS entries, 154,988 hashed bytes, 134–174-byte payloads (median 154). The log's 195 is the startup read count; the final cache includes later entries. The initial incorrect 195-final-count assertion failed and was corrected without changing parser results. Real-cache H1 host values are recorded under H1 above.
- **Correctness/integrity:** independent inventory verification passes 3,058/3,058 checks; six malformed variants fail closed and valid empty passes (7/7), and the supported/unsupported version-layout matrix passes 13/13. Validation scope is entry boundaries and SHA-1; DXVK retains semantic pipeline-field validation. Real-cache host timing recomputation passes 344/344, final ARM64 Check recomputation passes 547/547 over 360 raw rows, and the original input hash remains unchanged. Compact rows omit digest/success/inode fields, so those successful internal gates are not claimed as independently recomputed; the artifact blob returned EOF. Full per-entry and raw timing files are external under `phase-09/real-cache/`.
- **Disposition:** adopt the read-only inventory/benchmark tooling for regression evidence; no production runtime change. The real distribution confirms H2 materiality rejection and provides an exact-size H1 host result, not a phone loading/FPS claim.

### Historical Mali pipeline-trace audit

- **Purpose/hypothesis:** determine whether the tracked diagnostic trace supplies real pipeline-creation structure relevant to shader/cache decisions without inferring a current device A/B.
- **Exact inputs/environment:** read-only `pipeline_trace.log` SHA-256 `732af266ba9ce7abe6650a60559b5b2b2c20d6394743337e97fe5ed051903ea6`, matching D3D9/native logs, historical source `51e51ea3b20927e40e94135a6a09633c616dcf61`, logged Mali-G720 diagnostic tour. No new 0.16 device execution.
- **Raw result:** 171 graphics pipeline calls/342 stage records, zero logged request errors, eight compute calls, 80 unique VS and 87 unique FS hashes, 97 unique known pairs, 73 repeated-pair instances, maximum multiplicity six. Two sequence-6 stages have unavailable zero-byte records; the other 340 have logged structural validity. Rounded timing averages reconstruct approximately 8,644.1787 ms graphics driver, 4,613.23474 ms graphics wait, and 57.55823 ms compute driver time. Parser passes 713/713 structural checks; output is under `phase-09/pipeline-audit/`.
- **Correctness/limitations:** sequence, stage-count, request/error, hash-count, pair, size, and historical source identities are asserted. SPIR-V sizes are not original D3D9 bytecode SHA-1 inputs; no per-call timestamps, current FPS, cache-entry mapping, or concurrent registration proof exists. The 171 calls and 1,014 final cache entries are different lifetimes.
- **Disposition:** use as historical scale/structure evidence only. Do not change duplicate registration, Mali serialization, H2, or cache policy from pair repetition; no new runtime candidate or current-device claim.

### H3: full logged-description pipeline dedup audit

- **Hypothesis:** repeated complete logged pipeline descriptions, rather than repeated shader pairs alone, might reveal redundant driver compilation suitable for deduplication.
- **Baseline/candidate/files:** validated final head `39ff0f2c3b8bccc303e2d53e204df8abbc390d81`; no production candidate. Historical source `51e51ea3b20927e40e94135a6a09633c616dcf61` supplied `pipeline_trace.log`; patched DXVK base `c707d9026f33b6ab89639f154b6ac5f6326fa037` plus all 16 ordered Android patches supplied the dedup implementation. External read-only tools are `audit_pipeline_trace.py` and `audit_pipeline_dedup_source.py`.
- **Device/build/settings/scene/cache:** historical Mali-G720 diagnostic tour with its isolated tour cache/saves, not a new 0.16 run. Canonicalization removes only sequence numbers and module handles; layouts, shader hashes, and all logged state remain. No cold/warm current startup, FPS, quality, or driver A/B is inferred.
- **Raw result:** 171 calls produce 162 unique logged descriptions, eight duplicate-like groups/nine extra calls, maximum multiplicity three; only sequences 50/52 also have identical module handles. The trace omits specialization values, pipeline-cache/output handles, and destruction lifetimes. Source shows mutex-protected `m_fastPipelines` dedup whose key includes specialization map IDs and values. Extended trace/source audits pass 378/378 and 30/30.
- **Correctness/decision:** **reject a global/text-only dedup change; semantic duplication is inconclusive**. Identical log text can represent legitimate specialization variants, and existing DXVK already has a stronger semantic key. No runtime source changed and no saving is claimed.

### H4: real-cache exact duplicates and specialization variants

- **Hypothesis:** exact duplicate cache entries or repeated state prefixes might offer a safe loading/storage reduction; alternatively, the variants could demonstrate why naive pipeline dedup is unsafe.
- **Baseline/candidate/files:** read-only real cache at `39ff0f2c3b8bccc303e2d53e204df8abbc390d81`, SHA-256 `759f38aaa80f86e4bf283e52a51f4ce3316e6609a57b4cac841c5592518930b1`; no runtime candidate. External `audit_cache_variants.py` implements the source-derived v18 boundaries without writing the input.
- **Device/build/settings/scene/cache:** Python stdlib parser on x86-64; all 1,014 monolithic VS+FS entries, including their complete SHA-1 and specialization bytes. This is a captured cache inventory, not a fresh game scene, Android load, or cold/warm timing run.
- **Raw result:** 8,434/8,434 checks pass. There are zero exact duplicate payloads/SHA-1 hashes, 262 shader-key pairs, and 703 unique shader-plus-non-specialization states. 303 groups/311 extra entries differ only in specialization masks/values; 298 groups have two variants, two have three, and three have four. Repeated non-specialization prefixes total at most 44,750 bytes before any new metadata cost.
- **Correctness/decision:** every variable-length entry matches the v18 size model and every specialization tuple within a repeated-state group is distinct. **Reject exact-dedup/prefix-format optimization**: there are no exact duplicates, the theoretical prefix upper bound is small, and correctness-significant specialization variants cannot be merged. No cache format, warmup, or rendering behavior changed.

### CONTINUITY-2026-11-04-1249 — Phase 9 evidence reconstruction

- **Observation/recovery:** a fresh shallow clone preserved cumulative files but not the external store and initially pointed local `HEAD` at `00976d9`. Fetching the actual active branch and mixed-resetting to pushed `39ff0f2c3b8bccc303e2d53e204df8abbc390d81` restored HEAD/index without changing worktree bytes; exactly seven research files remained modified. No other branch was used.
- **Recovered raw evidence:** final run `41804622048`, check `126488886424`, and artifact metadata were retrieved through GitHub. A fresh verifier passes 390/390 over all 360 compact raw rows and reruns the real-cache inventory. Two verifier setup failures are preserved. All three attempted signed artifact downloads returned EOF; metadata/digests remain available and no retry loop followed.
- **CI disposition:** H1 run `41800212183`, H2 run `41802645649`, cleanup run `41803309517`, and final run `41804622048` are all completed successfully. Final `build-apk` passed and release was skipped; APK artifact metadata records SHA-256 `651c3326b0d36c12b0f405793d16c587c59f7031dd1c29ced2f10014ee24fc97`.
- **Decision:** older missing external raw files remain historical/unavailable; newly reconstructed evidence is labeled separately. Recovery itself is not a performance experiment and adopts no source change.

### Phase 10 H1: disabled DXVK Present diagnostic overhead

- **Hypothesis/source:** normal Play pays steady-clock and atomic-counter costs in `dxvk_presenter.cpp` even when `DXVK_PRESENT_STATS` is disabled. Gate only that instrumentation from the existing lazy D3D9 option read; do not change waits, acquire, queue present, FIFO, latency, or rendering.
- **Baseline/candidate/files:** baseline research commit `6e470f4eeb6efa570d8b3bb0006cc089bb4f0825`; pinned DXVK `c707d9026f33b6ab89639f154b6ac5f6326fa037` with all 16 Android patches. Candidate patch SHA-256 `e7fb2b1997e44110823403ff5faa07f951d32b69b94a895f79c9f0e65214ffec`; full patched presenter `a494e6d31aeb1d28f3cb1f1e9715a72a440ca8f2a62557c08603420a805a5154`. The now-retired tracked tools are archived externally under `retired-h1-tools/`; they extract actual baseline/candidate functions and reconstruct the target baseline. A nine-check follow-up proves the partial-baseline target parts identical to restored full Presenter `3051cf785c3f29a5f9d3dcc4e31d33466e345f1f8ec7a09c3745eece5bac9a38`; only an unrelated include/FIFO hunk differs outside the measured functions.
- **Host raw result:** full-source 24-round/1,000,000-call fixture gives queue pre-acquire medians 106.172747/9.096117 ns and paired saving 97.064736 ns (24/24); application-acquire fallback saves 103.675612 ns. A/A paired ratio 0.993155; enabled candidate/baseline 1.008219. All 48 raw rows and build/run logs are under `phase-10/present-host-full-final/`.
- **Correctness:** exact source transformation and D3D9 publication identity, call order, results, semaphore/fence state, feature chains, error paths, and counter gates pass. Full-source ASan+UBSan/TSan are clean; all 16 patches apply and existing DXVK memory/depth/FXAA/preflight/BC tests pass. Three failed harness setups are preserved and were not production failures.
- **Native ARM64 raw result:** run `41836560344`, Check `126597621495`; independent verification passes 210 checks over all 48 rows. Queue baseline/candidate medians 116.472338/13.836171 ns, paired saving 102.642780 ns (24/24); application fallback saves 107.708537 ns. A/A 0.999282; enabled 1.000611. Native semantics/publication and UBSan pass. This remains fake-Vulkan CPU timing, not Android/GPU/FPS/pacing.
- **Disposition:** **rejected as immaterial and reverted**. The saving is 0.000102643 ms/Present, 0.006159 ms CPU/s at 60 presents/s and 0.000616% of a 60 Hz interval. Production patch restored to SHA-256 `77cbc5c2e91fc866245534c0360d74719f86473fed41ae3fee257da813b31bab`; candidate tools/workflow removed from tracking and preserved externally with candidate commit `ed28afe` and raw evidence. Application fallback frequency is not assumed to be every frame.

### Phase 10 H2: historical Present-wall attribution audit

- **Hypothesis/inputs:** test whether the tracked tour supports a presentation-policy change. Read-only `proto-native.log` SHA-256 `35af134357452e1841e5ed9508035edd9e60cc54b0df3d7da6bf542667e858d1`, historical source `51e51ea3b20927e40e94135a6a09633c616dcf61`; no new device run.
- **Raw/integrity:** external `historical_present_audit.py` passes 1,229 checks over 60 periodic plus one final window. It exactly reconstructs 3,131 intervals, 3,132 wall calls/successful Presents, summary average/worst, and all threshold buckets. The first wall call is correctly retained as the interval baseline. Two parser-assumption failures are preserved before the passing audit.
- **Result:** rounded weighted interval/wall averages are 19.525278/0.838314 ms. Interval >50/100/250/1000 ms counts are 27/5/3/1 versus wall 3/1/1/0. Thirteen >50 ms-interval windows and three >100 ms-interval windows have wall max <8.5 ms. Window-average interval correlation is 0.783964 with EngineService and −0.211326 with wall.
- **Disposition:** reject FIFO/image-count/frame-latency/lock changes from this evidence. Rounded window correlation is not causation; wall is not GPU time, and low wall does not distinguish engine, draw, lock, GPU, or other non-Present work. No source changed for H2.

### Phase 10 H3: memory/submission compatibility audit

- **Hypothesis/source:** look for redundant memory/queue work in the exact restored all-16-patch renderer without weakening resource lifetime, unified-memory fallback, or Mali/Qualcomm behavior.
- **Evidence:** fresh pinned clone applies all 16 production patches and passes `git diff --check`, actual unified-memory selection, depth-resolve, FXAA, device-preflight, and BC-support tests. Source hashes/logs are under `restored-production-*`. Android managed-copy release already defaults on; queue/finish locking protects submission/lifetimes; memory maintenance has a 500 ms deadline and resource timing only wraps real waits.
- **Disposition:** no source change. No workload supports deleting synchronization, delaying maintenance under memory pressure, rewriting allocation, or altering managed-copy rules. The historical windows do not localize these costs, so no performance gain is claimed.

### Phase 10 H4: default present-blit and diagnostic-path audit

- **Hypothesis/source:** determine whether default present barriers, descriptors, shader selection, or pipeline diagnostics contain safely removable work. Exact restored all-16-patch source; no runtime candidate.
- **Evidence:** source audit plus `tools/test_dxvk_present_path.py` passes 46 checks. Three deliberate unsafe mutations (store op, descriptor count, trace gate) are detected and restored byte-for-byte. CI run `41839771476` / job `126607939927` step 26 passes the gate and all-seven-shader compilation; the full job remained in progress at the checkpoint. Local glslang/SPIR-V tools are unavailable. The structural pipeline trace has no timestamps and cannot be joined to frame stalls.
- **Disposition:** no runtime optimization adopted. Required store/layout transitions, uncovered-area clear, gamma descriptor validity, MSAA/color-space behavior, capability fallbacks, and Mali serialization remain. Heavy tracing stays tour-only and timing opt-in. Added regression/CI coverage only; no GPU/FPS claim.

### Phase 10 H5: frame-signal and limiter audit

- **Hypothesis/source:** determine whether present-wait/frame-thread or direct limiter work can be bypassed. Historical Mali feature log has `presentId=0`, `presentWait=0`, so direct signaling is the relevant path; exact restored source provides the pacing/lifetime semantics.
- **Evidence:** `tools/test_dxvk_frame_pacing.py` passes 25 checks. Four unsafe mutations are detected; one control-setup indentation failure is preserved. The audit verifies direct limiting/signaling, release/acquire publication, destruction order, FIFO present waits, sleep outside the mutex, bounded refresh heuristic, initial frame ID/fence, repeat suppression, and present-ID/present-wait coupling.
- **Disposition:** no runtime candidate. Skipping these operations changes reconfiguration, queue-latency, or lifetime behavior without a measured bottleneck. Structural tests are not FPS, input-latency, or physical-device pacing evidence.

### Phase 10 H6: Android-owned GPU paths and exact pipeline-state census

- **Hypothesis/source:** determine whether Android-owned D3D9/DXVK changes add avoidable GPU synchronization/upload work and whether the real pipeline trace exposes repeated presentation-pipeline construction or unsafe MSAA/pipeline behavior. The 16-patch restored tree is unchanged.
- **Evidence:** `android-gpu-owned.diff` enumerates project-owned changes; `audit_android_owned_gpu.py` passes **27 checks** over ownership, six byte-identical core queue/command-list/image implementation/header pairs, barrier fields, upload helpers, and capability gates. `census_pipeline_gpu_state.py` passes **2,238 checks** over trace SHA-256 `732af266ba9ce7abe6650a60559b5b2b2c20d6394743337e97fe5ed051903ea6` and **171** graphics pipeline calls. Parser attempts 1–4, including the final whitespace-tolerant dynamic-state correction, remain external.
- **Raw structural result:** all 171 pipelines are single-sample; 15 have zero color attachments and 156 have one. Formats are 136 B8G8R8A8_UNORM, 19 R16G16B16A16_SFLOAT, one R8G8B8A8_UNORM, and 15 none. The sole exact present-blit structural signature is sequence 6: two stages, triangle list, no vertex bindings, one R8G8B8A8_UNORM target, disabled blend/depth, full write mask, and exact Android dynamic states. There are zero repeated matching creations. Sequence 6 remains a strong structural match, not handle-proven presenter identity. No MSAA pipeline coverage exists.
- **Disposition:** **no GPU runtime change**. Android-owned changes retain sample-zero depth resolve, optional managed-copy destruction, null-descriptor resources, transform-feedback gating, dummy fragment fallback, unified-memory preference, and the ARM compiler-thread cap; they do not alter barrier calls or stage/access/layout fields. No project-owned barrier/upload fast path was found. Repeated-pipeline elimination is already structurally consistent with caching; no second cache is justified. Do not infer GPU time, draw frequency, MSAA cost, or visual equivalence from creation state, and do not reduce samples/formats/effects.

### Phase 10 H7: historical whole-process memory windows

- **Hypothesis:** determine whether the historical diagnostic tour shows sustained memory growth or pressure that justifies allocator/managed-copy changes.
- **Method:** `audit_historical_memory.py` paired all 61 `diag: memory` records with the exact H2 frame windows and passes **551 checks**. It audits whole-process RSS, peak, virtual, anonymous/file-backed memory, swap, system availability, and arena figures; it does not isolate DXVK or GPU allocations.
- **Raw result:** RSS ranges from 691 MiB to a 2,021 MiB peak and ends at 2,018 MiB. The first 10 windows span 9.098 seconds and grow RSS **691→1,900 MiB (+1,209)**, file-backed memory **446→1,420 MiB (+974)**, and arena **362→570 MiB (+208)** during loading. The last 20 windows span 19.116 seconds: RSS **2,018→2,018 MiB (net 0; range 2,015–2,020)**, arena **586→587 (+1)**, virtual **27,765→27,934 (+169)**, swap unchanged at 113 MiB, and system available **2,564→2,721 MiB (+157)**. Global minimum system-available is 2,203 MiB during early loading. The 1,032.764 ms `shadow_motion` window has RSS +22 MiB, virtual +328 MiB, arena +0 MiB from the previous window and Present wall maximum 1.129 ms; those co-occurrences are not a causal attribution.
- **Disposition:** **no memory-policy change**. Early loading growth is not a leak; late stable RSS is not proof of no leak or pressure. Late virtual growth remains visible. This approximately one-minute instrumented tour is not a current A/B or long soak, so it cannot justify changing unified-memory selection, managed-copy destruction, allocation chunks, or resource lifetimes. Phase 12 needs a longer current-device memory/GPU-time soak before any such claim.

### Phase 10 H8: exact logged Mali allocator fixture

- **Hypothesis:** determine whether the unified-memory selection backport preserves the actual Mali-G720 heap as well as reported Adreno heaps and real system-memory fallback.
- **Implementation:** `tools/test_dxvk_unified_memory.py` now compiles the actual patched `determineMemoryTypesWithPropertyFlags` under UBSan with the logged Mali-G720 MC7 49.1.0 types `0x7`, `0xb`, `0x11`, and `0x21`. It checks host-coherent type `0x1`, host-visible types `0x3` (matching the logged buffer-compatible mask), all-four device-local mask `0xf`, and cached-coherent fallback to `0x1`. Existing Adreno and real-system-memory checks remain.
- **Result/control:** patched-source execution passes all 14 named assertions and an exhaustive **160,000-configuration / 2,560,000-mask** sweep under UBSan. An independent Python model recomputes every expected mask and matches the actual FNV-1a digest `b2c31cdd46d09505` with 10 integrity checks. The sweep includes all 16 request values and lazy/protected real-heap variants; cached-coherent fallback is exercised 104,976 times. The same named fixtures against pristine upstream `dxvk_memory.cpp` fail **8 assertions**, including every Adreno transfer-buffer pair, the Mali host-visible mask, and system-memory preference; the upstream control stops before the exhaustive block. Exact outputs and source hashes are in external `unified-memory-control.json`; one unavailable `/usr/bin/time` wrapper failure is preserved.
- **Disposition:** **adopt regression coverage; no production allocator change**. This is host functional coverage derived from a historical device heap plus exhaustive model verification, not an allocation benchmark, current-device rerun, GPU timing result, or memory-pressure verdict.

### Phase 10 completion disposition

Phase 10 completed at `2026-11-04T14:17:19.094775Z`, **4,428.332251 seconds** after start. Validation head `7c4b986d91ca0dbd0987c2679f720ab8733d6c08` has no net production runtime delta from the Phase 9 completion; only three test tools and workflow coverage changed outside research documents. Fresh broad regression passes 13/13, policy 5/5, and all four audits rerun byte-identically (4,045 reconstructed checks). CI run `41841712692` completed successfully with APK artifact SHA-256 `fd46e7bbde44c6030dc187f59fef0db0ebdb03c62c6026499ea0c6cbebc1c661`; release was skipped. H1 remains rejected/reverted, H2–H8 retain no-runtime-change dispositions, and no physical-device FPS/pacing/GPU-time result is claimed. Phase 11 texture/Bink/audio work is next.

## Phase 11 texture/Bink/audio candidates

### H1: regenerated Bink decode/registered-plane baseline

- **Hypothesis:** a corrected, fresh production-source cohort can replace unavailable historical raw profiles and determine whether registered-plane work is material enough to justify a narrowly scoped candidate.
- **Implementation:** evidence-only harness includes the unmodified production Bink tests/sources, reserves the guest call stack before the 15.5 MB compressed input, times only `BinkDoFrame`, and performs YV12 export/hash, full-input comparison, both-set plane comparison, and `NextFrame` outside the timer. Twenty balanced pairs use fresh processes and alternate path order.
- **Result/control:** unmodified UBSan suites first pass at 32,000 and 44,100 Hz with empty stderr. The normal 40-process cohort decodes 11,640 frames with common digest `9e46ff3727b5d03a`; independent recomputation passes 23,713 checks. Median 291-frame totals are 525.858246 ms unregistered and 650.740283 ms registered. Median paired ratio is 1.233614 and paired delta 125.535526 ms (~0.431394 ms/frame); median per-run frame p95 is 4.309956/4.730767 ms and p99 4.857595/5.254400 ms. Every registered row and all compressed inputs remain exact.
- **Disposition:** **baseline established; no source candidate yet**. This is not pure upload attribution or Android playback/FPS evidence. Existing tight matching-pitch copies are already collapsed; the measured negative/padded layout still requires row-wise work, and both populated sets are required by current unlock/liveness semantics. Do not copy padding, reverse rows as a block, drop a plane set, or cache movable guest pointers without new evidence.

### H3: Bink audio ring-wrap candidates

- **Hypothesis:** replacing per-sample 64-bit ring modulo with a one-wrap conditional or two contiguous spans can reduce actual callback CPU without changing samples, gains, bins, consumption, or A/V anchoring.
- **Implementation/control:** the production callback runs with one live movie/track, 1,024 stereo float frames, nontrivial gain/pan, direct/bin and wrap/nowrap cases. Every output bit and consumed range is checked outside the timer against an independent scalar reference. Twelve balanced rounds compare baseline, identical A/A control, conditional subtract, and span reads across 32/44.1/48 kHz: 240 processes, 960,000 timed callbacks, and 962,723 independent raw-data checks.
- **Result:** conditional-subtract/baseline medians are 1.025–1.131 (slower everywhere), with a loop-carried `cmov` chain in disassembly. Span/baseline medians are 0.778143/0.783329/0.780066/0.778607/0.793140 across the five configurations, with 11–12 wins out of 12, but absolute savings are only 0.126010/0.126436/0.130193/0.118885/0.077946 ms CPU/s. Every output digest is exact.
- **Disposition:** **reject conditional subtract; span not adopted on x86-64**. Span is below the preregistered 0.25 ms CPU/s absolute threshold in all configurations, so its ratio alone cannot justify a runtime change. A temporary exact-source native ARM64 job is pending to avoid rejecting a target-relevant gain from x86 timing alone. Production source is unchanged.
