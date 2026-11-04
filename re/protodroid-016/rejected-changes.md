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

- **Phase 4 split key/function arrays:** provisional integration was reversed and the original runtime restored. Independent shuffled localized workloads reproduced regressions: split/original paired medians 1.035959 at 16 targets, 1.034367 at 256, and 1.157686 at 16,384; mixed/small-dispatch results did not establish a beneficial real target mix. Lookup16 was slower in 12/12 pairs in both protocols, while lookup4096 improved in 11/12 and 10/12, confirming a workload tradeoff rather than a universal direction. Standard cold/miss gains remain valid synthetic evidence but are insufficient for adoption.
- **Phase 4 parallel indexed 32-bit hash slots:** rejected for adoption despite lower cold/memory and some warm gains. The initial zero-target association bug was corrected and preserved; the valid candidate regressed repeated-target lookup (paired candidate/original median 1.052112) and had variable other results. No split or indexed layout remains in production.
- **Phase 4 flat direct table:** rejected analytically. Real entries lack uniform alignment and span 14,199,324 bytes; byte-granular pointers alone would require 113,594,592 bytes, versus 3,145,728 bytes for the split hash.
- **Phase 4 17-bit table:** rejected from exact probe behavior (12.873683 mean successful and 446.796510 mean synthetic-miss probes) plus a separately labeled bounded smoke. Its near-full load factor is not a safe performance/memory trade.
- **Phase 4 19-bit table:** not adopted. It improves some warm modes but has 1.525857× paired cold median, adds about 3.2 MiB host maxRSS, and does not improve repeated-target lookup; no real target mix justifies the tradeoff.
- **Phase 4 static-BSS storage:** inconclusive and not adopted. Same-size static storage has small variable gains, a repeated-target regression, and unknown Android loader cost after moving 3,145,760 bytes into host ELF NOBITS.
- **Phase 4 alternative hash constants:** no candidate promoted. Eight constants across four miss models produced small workload-dependent tradeoffs rather than a robust improvement; timing one synthetic winner would overfit the fixture.
- **Phase 4 interleaved index8/packed12/bucket2/bucket4:** rejected after standard plus shuffled-locality screening. Index8 reached 1.808098× at 16,384-target lookup and was slower in 12/12 pairs; packed12 did not improve standard hits and regressed several localized cases; buckets added indexing costs and retained repeat/locality regressions (repeat and 16,384-target lookup each slower in 11/12 pairs for both bucket sizes). The layout search stops rather than overfitting more variants.
- **Phase 4 TSO/alignment relaxation:** not adopted. All 192 generated files and one AArch64-musl partition provide static structure only; acquire/release/DMB counts are not dynamic costs and cannot justify weakening guest-memory ordering.
- **Phase 4 Python-launched cold RSS:** rejected as invalid memory evidence because children inherited the Python parent's 16,204-KiB high-water floor. Valid reported cold/RSS comparisons use direct-shell launches; raw invalid logs remain preserved.
- **Phase 4 instruction-count or musl-to-device conversion:** rejected. The production/measurement assembly match is structural only; it does not establish Android/Bionic equivalence, cycles, FPS, pacing, or device memory.
- **Phase 5 rowtile32:** rejected after weaker/inconsistent controlled timing than direct32; exact-output success alone was not enough for adoption.
- **Phase 5 noinline direct32 layouts:** rejected. Early/late helper placement did not improve the overall trade; late noinline regressed DXT1-32 by about 2.9% and was slower for DXT3 than the selected inline form.
- **Phase 5 lazy per-pixel alpha:** rejected despite full normal/ASan+UBSan output equivalence. DXT3/DXT5 BGRA were slower than precomputed direct32 alpha; incidental unchanged-path layout improvements were not treated as algorithmic wins.
- **Phase 5 direct packed16 decoder integrations:** rejected in all four forms. They were output-correct, and packed16 was often faster, but repeated screening A/Bs regressed the selected BGRA path by roughly 38–80%; no long cohort was wasted on those integrations. The separate palette-alpha simplification achieved packed16 gains without that material BGRA cost and is not part of this rejection.
