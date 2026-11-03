# Phase 2 research checkpoint — five-repository performance audit

Status: **active; substantive audit checkpoint saved, not a phase-completion report**. The one-hour minimum has not elapsed. No physical-device validation or production optimization is claimed.

## 1. Actual time and work

- Started: `2026-11-03T15:58:37+00:00`.
- Checkpoint clock reading: `2026-11-03T16:09:18+00:00`.
- Active elapsed time at that reading: **10 minutes 41 seconds**, with no idle-time or elapsed-time padding.
- Cloned and pinned all five required repositories read-only; inspected implementation and evidence documents, compared shared renderer lineage, checked license/provenance, and compared attractive techniques with existing ProtoDroid code.
- Directly inspected the pinned DXVK 2.5.3 dependency to verify its existing compiler workers, warm-cache compilation, cache writer, frame-latency control, CS batching/dirty-state paths, and staging allocation/throttling instead of inferring them from patch comments.
- Populated `repository-ledger.md` and `technique-matrix.md`; recorded explicit audit-level non-actions in `rejected-changes.md`.

## 2. Files and inspected revisions

- PrototypeRecomp `c9224ecd1b7c422d5fa498c5fd541fabeb38858f`: GDFX reads, filesystem/APC completion, guest threads, and Xenon packet processing.
- Black-mobiu `eaab755566185f4d271c71470d6b35d26a4e59aa`: SPIR-V cache/glslang, Vulkan features/pipeline persistence, render batches/state delta/readback.
- GoW III fork `b3a7c3d5e916cf8f4622192c0faa9383ebd77c00`: async compiler, pipeline cache/serialization, draw preparation, scheduler, staging, shader HLE, PC and Android evidence documents.
- ISS Android `43c833deb37bc504bf099bbf707a5440103a8cae`: preparation-only Android documents plus real inherited renderer implementations and upstream identity.
- Bloodborne Android `f241353e36f46bf183806acd7b79222afff2fd2f`: strict AOT dispatch, unified-memory paths, blocking waits, staging, Adreno device/session results, and failure records.
- Compared local `src/native/runtime.cpp`, `src/recomp/bridge_x86.cpp`, DXVK state-cache/compiler/serialization patches, RCF/Bink paths, and scheduling code.
- Baseline dependency DXVK `v2.5.3` / `c707d9026f33b6ab89639f154b6ac5f6326fa037`: `dxvk_pipemanager.cpp`, `dxvk_state_cache.cpp`, and D3D9 frame-latency implementation read directly.
- Exact file lists, source hashes, licenses, commit provenance, DXVK key-file hashes, and lineage results: `/tmp/protodroid-016/audit`.

## 3. Technique decisions

- **Adopted:** none. No runtime/production file changed in this checkpoint.
- **Useful candidates for later measurement:** bounded compiler/worker contention, short-spin-then-block only at a demonstrated wait hotspot, staging retention only at a demonstrated allocation/first-touch hotspot, and narrow residual state/cache work only after D3D9/DXVK profiling.
- **Already present, not new gains:** open-addressed multiplicative-hash guest dispatch, two-thread Mali compiler cap, DXVK compiler/warm-cache workers and durable cache writing, CS batching/dirty-state handling, staging suballocation/throttling, and capability-driven Vulkan behavior.
- **Rejected at audit:** direct PS4 unified-memory/KGSL import, 128/512 MiB staging defaults, renderer/FEX/strict-AOT migration, treating PrototypeRecomp guest `PENDING` as host async I/O, treating Black-mobiu glslang as asynchronous, game-specific HLE/SRT import, and frame generation/upscaling/quality reduction as engine-FPS evidence.
- **Inconclusive:** every technique's actual ProtoDroid FPS, frame-time, loading, visual, memory, and thermal effect.

## 4. Raw measurements

No new ProtoDroid performance run was performed in this source-audit checkpoint, so there is no before/after optimization result. Phase 1 B0 raw measurements and hashes remain under `/tmp/protodroid-016/baseline`.

External numbers were copied only with their context into `repository-ledger.md`: GoW III PC test-scene results, Bloodborne Thor AOT/FEX and memory/FPS results. They are not ProtoDroid or generic Android evidence. The ISS/GoW III file-similarity calculation is source-provenance evidence, not a performance measurement; raw output is `/tmp/protodroid-016/audit/shared-lineage.txt`.

## 5. Correctness

- No production behavior, setting, renderer, cache format, or ABI changed.
- Source claims were traced to concrete functions rather than accepted from README summaries.
- Existing local dispatch/compiler/cache implementations were read directly before classifying external techniques as duplicates.
- No external project's test suite was run and no external binary was executed; this checkpoint establishes source understanding, not external correctness on Android.
- All visual, gameplay, mod, FIFO/Reset, audio, cache-corruption, and physical-device correctness gates remain open for any future experiment.

## 6. Limitations

There is no Android handset, ADB, GPU profiler, SimplePerf, or Perfetto session available. The external projects target different games, guest ISAs, renderers, drivers, and test settings. GoW III and ISS renderer files share substantial lineage and cannot be counted as independent validation. Several source documents explicitly describe unvalidated Android ideas, and Bloodborne's controlled results are Adreno-specific with Mali unsupported at the pinned revision. License review permits source reading here but no external code has been incorporated.

## 7. Next priority

Continue Phase 2 until its required active-time and audit gates are genuinely satisfied; do not mark it complete from this checkpoint. Then Phase 3 should profile ProtoDroid's existing pipeline wait/driver time, Present/pacing, guest/render threads, native hot paths, uploads, and cache I/O. Physical-device FPS/pacing/thermal/visual evidence remains the primary unavailable gate.
