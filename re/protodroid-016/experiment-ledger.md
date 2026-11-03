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
