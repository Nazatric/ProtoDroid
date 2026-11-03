# Phase 2 research checkpoint — five-repository performance audit

Status: **active; substantive audit checkpoint saved, not a phase-completion report**. The one-hour minimum has not elapsed. No physical-device validation or production optimization is claimed.

## 1. Actual time and work

- Started: `2026-11-03T15:58:37+00:00`.
- Checkpoint clock reading: `2026-11-03T16:19:50+00:00`.
- Active elapsed time at that reading: **21 minutes 13 seconds**, with no idle-time or elapsed-time padding.
- Cloned and pinned all five required repositories read-only; inspected implementation and evidence documents, compared shared renderer lineage, checked license/provenance, and compared attractive techniques with existing ProtoDroid code.
- Directly inspected the pinned DXVK 2.5.3 dependency to verify its existing compiler workers, warm-cache compilation, cache writer, frame-latency control, CS batching/dirty-state paths, staging allocation/throttling, and resource-wait sync counters instead of inferring them from patch comments.
- Ran focused host correctness tests for GoW III, ISS, and Bloodborne at their exact pins, including async queues, warm-up/cache helpers, AOT dispatch/strict misses, and UMA promotion/demotion stress; preserved commands, raw logs, binaries, environment, and hashes outside Git.
- Populated `repository-ledger.md` and `technique-matrix.md`; recorded explicit audit-level non-actions in `rejected-changes.md`.
- Continuation began at `2026-11-03T17:41:53Z`; completion duration is not claimed at this checkpoint. The original `/tmp/protodroid-016` was absent after sandbox recreation. Restored every exact external pin and DXVK under persistent `/home/user/protodroid-016-evidence`, regenerated explicit new manifests/provenance, and retained `/tmp/protodroid-016` only as a compatibility symlink. No original raw log is claimed recovered.
- Added and ran fresh production-code behavior probes for Black-mobiu's real `vk_shader.cpp` (stubbed glslang) and PrototypeRecomp's real GDFX implementation; regenerated the focused GoW III/ISS/Bloodborne tests. Preserved setup/test-fixture failures and corrected reruns, including two TSan harness-lifetime findings.

## 2. Files and inspected revisions

- PrototypeRecomp `c9224ecd1b7c422d5fa498c5fd541fabeb38858f`: GDFX reads, filesystem/APC completion, guest threads, and Xenon packet processing.
- Black-mobiu `eaab755566185f4d271c71470d6b35d26a4e59aa`: SPIR-V cache/glslang, Vulkan features/pipeline persistence, render batches/state delta/readback.
- GoW III fork `b3a7c3d5e916cf8f4622192c0faa9383ebd77c00`: async compiler, pipeline cache/serialization, draw preparation, scheduler, staging, shader HLE, PC and Android evidence documents.
- ISS Android `43c833deb37bc504bf099bbf707a5440103a8cae`: preparation-only Android documents plus real inherited renderer implementations and upstream identity.
- Bloodborne Android `f241353e36f46bf183806acd7b79222afff2fd2f`: strict AOT dispatch, unified-memory paths, blocking waits, staging, Adreno device/session results, and failure records.
- Executed the focused test files listed in `repository-ledger.md` at those same commits. Fresh work adds Black-mobiu cache/control-flow probes and PrototypeRecomp's own `scripts/test_gdfx_volume.cpp` plus a custom unchanged-production `GdfxVolume::Read` probe. Black's checked-in launcher/touch tests were not substituted as renderer evidence; PrototypeRecomp's synthetic GDFX fixture is not represented as original game data.
- Compared local `src/native/runtime.cpp`, `src/recomp/bridge_x86.cpp`, DXVK state-cache/compiler/serialization patches, RCF/Bink paths, and scheduling code.
- Baseline dependency DXVK `v2.5.3` / `c707d9026f33b6ab89639f154b6ac5f6326fa037`: `dxvk_pipemanager.cpp`, `dxvk_state_cache.cpp`, and D3D9 frame-latency implementation read directly.
- Fresh current file lists, source hashes, licenses, commit provenance, DXVK key-file hashes, lineage results, commands, raw logs, and integrity output: `/home/user/protodroid-016-evidence/audit`. These are regenerated artifacts; the lost original manifest set is not claimed byte-identical.

## 3. Technique decisions

- **Adopted:** none. No runtime/production file changed in this checkpoint.
- **Useful candidates for later measurement:** bounded compiler/worker contention, short-spin-then-block only at a demonstrated wait hotspot, staging retention only at a demonstrated allocation/first-touch hotspot, and narrow residual state/cache work only after D3D9/DXVK profiling.
- **Already present, not new gains:** open-addressed multiplicative-hash guest dispatch, two-thread Mali compiler cap, DXVK compiler/warm-cache workers and durable cache writing, CS batching/dirty-state handling, staging suballocation/throttling, and capability-driven Vulkan behavior.
- **Rejected at audit:** direct PS4 unified-memory/KGSL import, 128/512 MiB staging defaults, renderer/FEX/strict-AOT migration, treating PrototypeRecomp guest `PENDING` as host async I/O or importing its global GDFX read mutex, treating Black-mobiu glslang as asynchronous or its SPIR-V cache format as a DXVK drop-in, game-specific HLE/SRT import, and frame generation/upscaling/quality reduction as engine-FPS evidence.
- **Inconclusive:** every technique's actual ProtoDroid FPS, frame-time, loading, visual, memory, and thermal effect.

## 4. Raw measurements

No new ProtoDroid performance run was performed in this source-audit checkpoint, so there is no before/after optimization result. Phase 1's recorded historical numbers remain in `phase-01.md`/`baseline.md`; their original `/tmp/protodroid-016/baseline` raw artifacts were also lost and are not claimed currently available.

External numbers were copied only with their context into `repository-ledger.md`: GoW III PC test-scene results, Bloodborne Thor AOT/FEX and memory/FPS results. They are not ProtoDroid or generic Android evidence. Fresh character-level ISS/GoW III similarity output is `/home/user/protodroid-016-evidence/audit/shared-lineage.txt`; it is provenance evidence, not performance measurement, and is not presented as byte-identical to the lost earlier script output.

Fresh focused external tests are correctness/behavior evidence, not FPS results:

- GoW III: five native plus all 20 Python/helper checks pass with isolated Unicorn 2.1.4; all five native tests also pass ASan/UBSan. ISS: corresponding five native tests pass normally and under ASan/UBSan. Page-state and pipeline-binary tests remain unrun for their recorded dependency reasons.
- Bloodborne: all three AOT tests and both UMA configurations pass with zero lost writes. Fresh longest compaction/demotion-hammer freezes are **12.929/0.51 ms at 256 KiB** and **17.588/11.98 ms at 2048 KiB**. The earlier historical run's 11.931/0.61 and 21.700/13.54 ms remain a separate result. None is a phone frame time.
- Black-mobiu: six direct orchestrated cold misses caused six serialized stub compilations; the subsequent warm hit avoided compilation. Final ordinary/ASan-UBSan/TSan stress runs produced 64 files with 86/80/79 total compilations. This is direct component behavior, not proof of duplication through the actual module-memoizing render caller. A four-byte opaque cache entry was accepted by the loader; real Vulkan module creation remains untested.
- PrototypeRecomp: its own GDFX regression test passes with six synthetic files/two directories and zero byte-identity mismatches. The custom probe verified 8,388,745 bytes through 617 irregular reads; eight threads made 1,024 observed 16 KiB reads with zero mismatches and `max_active_pread=1`. Equal 64-call synthetic-delay work took 17.845 ms single-threaded versus 18.587 ms with four threads (ratio 1.042). Missing synthetic backing data returned 4,096 zero bytes with a full-length success result, exactly as implemented.

Exact fresh commands, environment, raw successful/failed logs, binaries, harnesses, fixtures, and hashes are under `/home/user/protodroid-016-evidence/audit/tests` and `audit/test-artifact-hashes.txt`. No host number above is ProtoDroid FPS, device loading time, GPU time, or thermal evidence.

## 5. Correctness

- No production behavior, setting, renderer, cache format, or ABI changed.
- Source claims were traced to concrete functions rather than accepted from README summaries.
- Existing local dispatch/compiler/cache implementations were read directly before classifying external techniques as duplicates.
- Fresh focused host tests pass at all five pinned revisions as detailed above; these establish selected synthetic/host correctness only, not full-suite or Android correctness. Six missing GoW sparse dependencies were materialized from exact `HEAD` after preserving the failed run; a 19-pass/one-skip rerun was not relabeled as 20 passes. Installing Unicorn and rerunning produced the full pass. ISS used the correct include root from the outset in this fresh run.
- GoW III's original async TSan run exposed a fixture `shared_future` lifetime race; a preserved one-line test-only `heavy.Stop()` correction passes TSan. Black's original TSan run exposed packed `vector<bool>` harness writes; independent-byte storage fixed the harness and final TSan passes. Initial Black stub/expectation failures and the GoW sparse pathspec failure are also retained. No external production source was edited, and no original TSan failure is counted as clean.
- GoW III/ISS page-state and pipeline-binary results are not claimed. No external test was run on Android, and no Vulkan/phone GPU test was inferred from host success. Black uses stubbed glslang and PrototypeRecomp uses synthetic data. All visual, gameplay, mod, FIFO/Reset, audio, cache-corruption, and physical-device correctness gates remain open for any future ProtoDroid experiment.
- Historical audit integrity passed at `2026-11-03T16:19:31Z`, but its raw output is unavailable after recreation. Fresh integrity verification passed at `2026-11-03T17:56:56Z`: all six exact pins and clean tracked worktrees, every current regular-file/symlink manifest, all audit-manifest hashes, 107 test-artifact hashes, the persistent-store symlink, and the unchanged original DLL hash. Current output is `/home/user/protodroid-016-evidence/audit/integrity-verification.log`.

## 6. Limitations

There is no Android handset, ADB, GPU profiler, SimplePerf, or Perfetto session available. The external projects target different games, guest ISAs, renderers, drivers, and test settings. GoW III and ISS renderer files—and the five focused native test files—share substantial or identical lineage and cannot be counted as independent validation. Several source documents explicitly describe unvalidated Android ideas, and Bloodborne's controlled results are Adreno-specific with Mali unsupported at the pinned revision. Black's glslang is stubbed, its direct concurrency probe bypasses caller memoization, and PrototypeRecomp's `pread` delay is synthetic; neither establishes real shader/rendering/loading performance. Original `/tmp` raw artifacts are unavailable; only explicitly regenerated current evidence is cited as present. License review permits source reading here but no external code has been incorporated.

## 7. Next priority

Continue Phase 2 until its required active-time and audit gates are genuinely satisfied; do not mark it complete from this checkpoint. Then Phase 3 should profile ProtoDroid's existing pipeline wait/driver time, Present/pacing, guest/render threads, native hot paths, uploads, and cache I/O. Physical-device FPS/pacing/thermal/visual evidence remains the primary unavailable gate.
