# Rejected changes and non-actions

This record includes both standing non-actions and experimentally rejected candidates.

## Standing non-actions

- Fake FPS values, generated-frame inflation, hidden resolution/texture/effect reductions, frame skipping, and unconditional movie skipping.
- Presenting host or ARM64-runner microbenchmarks as physical-device FPS, GPU time, latency, or thermal results.
- Spoofing Vulkan features/extensions or equating extension presence with feature support.
- Wholesale FEX, SDL3, renderer, DXVK, or engine replacement without a specific measured need and compatibility plan.
- Copying external code before license/provenance review.
- Treating `iss_android` preparation or another project's README as proof of Android performance.
- Sacrificing dynamic shadows, Bink A/V, compatible mods/saves, Classic controls, FIFO/Reset safety, or independent AA policy for performance.
- Building an APK for every small edit or releasing/merging 0.16 without explicit final authorization.

## Rejected evidence combinations

- `TestingFiles` `b2ca160b…` `proto-native.log` plus `journal.log`: rejected as a single run because their embedded sources are `54ef821e…` and `e34ebb06…`. Separate parsing is retained for historical inspection only.
- Root `51e51ea…` logs as released-0.15 results: rejected because the source revision does not match the release.

## Phase 2 audit-level non-actions

These are source/applicability decisions, not controlled performance experiments:

- **Duplicate dispatch rewrite:** rejected. Bloodborne's `0x9E3779B1` open-addressed lookup is already structurally present in both ProtoDroid recompilation dispatch paths.
- **Extra Mali compiler workers or a second warm-up architecture:** rejected without new evidence. ProtoDroid already caps Mali workers at two; affected Mali driver pipeline calls remain serialized for correctness. Direct DXVK 2.5.3 inspection also confirms existing lowest-priority pipeline workers, warm-cache compilation, and a separate cache writer.
- **Black-mobiu “async” shader or cache-format import:** rejected because the pinned glslang path is synchronous on a cache miss. Fresh direct component stress confirms global serialization, but does not prove game-workload duplication through the caller's module memoization/render worker. Its SPIR-V loader/format and D3D8 renderer are not DXVK drop-ins; DXVK's SHA-1-protected pipeline-state entries are a different layer.
- **PrototypeRecomp “async host I/O” or GDFX locking import:** rejected because host reads/writes finish synchronously before guest `PENDING`/APC/event completion. Fresh production-code observation confirms `io_mutex_` serializes `pread`; ProtoDroid's per-fd read path has no equivalent global lock. GDFX region lookup is not a reason to replace the existing RCF index, and its full-length zero-fill on missing backing data must not be used to hide corruption.
- **Generic batching/state-tracking/staging-reuse rewrite:** rejected as a duplicate proposal. Direct DXVK inspection confirms recycled CS chunks, a CS thread, change/dirty-state handling, linear staging suballocation, and an in-flight staging threshold. Only a measured residual cost could justify a narrow change.
- **GoW III/ISS large staging defaults:** rejected. A 128 MiB prewarm and 512 MiB retention policy has no ProtoDroid memory/stall justification and is not equivalent to DXVK's existing staging-in-flight metric.
- **Speculative draw-pipeline import:** rejected as a direct patch. Its PC evidence is not Android evidence, the GoW III Android configuration disables preparation workers by default, and D3D9/DXVK has a different architecture.
- **Bloodborne unified-memory/KGSL/AOT/affinity import:** rejected. UMA showed memory savings without automatic FPS improvement and includes serious coherence/reset risks; unmodified focused UBSan also stops on a zero-length null-`memmove` in `runtime_memory.c::vma_erase`, and only a preserved test-only zero-byte-guard copy completes both UBSan configurations. UMA TSan also remains negative/unresolved: the original fixture has a stop-flag race, and a test-only atomic-stop rerun reaches a writer/`MAP_FIXED` model report rather than a clean pass. No external source fix or ProtoDroid patch is adopted. AOT was approximately tied with FEX at a GPU-bound save; its variants test also retains a two-`HookRec` LSan failure (with only UBSan-only passing), so hook-record lifetime/replacement growth would need resolution rather than concealment. Fixed affinity is not justified without ProtoDroid thread evidence. This does not reject or remove ProtoDroid's unrelated upstream DXVK memory-type-selection compatibility backport in `dxvk-unified-memory.patch`.
- **GoW III readback page-watcher/queue import:** rejected as incompatible with D3D9/DXVK resource lifetime. Existing DXVK/Android Present diagnostics already expose aggregate GPU/CS sync timing; only a profile-driven need for finer attribution remains open.
- **Generated/upscaled frames as performance evidence:** rejected. Bloodborne's 29.2 game-FPS result is not 60 real FPS, and GoW III PC/upscaler scenes are not ProtoDroid phone results.
- **ISS as independent confirmation:** rejected. Its Android port is preparation-only, and its async/draw/staging implementations share substantial lineage with the GoW III fork.

## Phase 3 rejected evidence uses

- **Optimized RCF gprof call graphs:** rejected for attribution because impossible/unreachable arcs persisted in PIE and non-PIE diagnostics. Limited reachable flat PC samples remain only with startup/teardown/harness exclusions.
- **Historical heavy pipeline trace as clean timing:** rejected because shader dumps and `fsync` ran inside the compile lock for all 171 graphics calls. Future timing must separate `pipeline_trace=0` from structural tracing.
- **Original Bink decode cohort:** rejected after proving the evidence-only guest stack overlapped input at offset 3,145,712. Its old digest/timings and resulting frame-73 failure are not production evidence; corrected dedicated-stack cohorts are retained separately.
- **Cross-harness or instruction-count rankings:** rejected. DXT, mixer, RCF, Bink, and historical windows have no common frame denominator, and AArch64 instruction counts are not cycle/device shares.
- **Ascending-only destination-slot interpretation:** rejected as confounded by order/frequency. The retained result uses three ascending plus three descending repetitions and paired ratios, still without claiming cache causation or ARM64 transfer.

## Experimentally rejected candidates

None yet; no optimization experiment has been run in 0.16.
