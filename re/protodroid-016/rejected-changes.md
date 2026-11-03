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
- **Extra Mali compiler workers:** rejected without new evidence. ProtoDroid already caps Mali workers at two; affected Mali driver pipeline calls remain serialized for correctness.
- **Black-mobiu “async” shader import:** rejected because the pinned glslang path is synchronous on a cache miss. Its renderer/cache architecture is not a DXVK drop-in.
- **PrototypeRecomp “async host I/O” import:** rejected because host reads/writes finish synchronously before guest `PENDING`/APC/event completion. GDFX region lookup is not a reason to replace the existing RCF index.
- **GoW III/ISS large staging defaults:** rejected. A 128 MiB prewarm and 512 MiB retention policy has no ProtoDroid memory/stall justification.
- **Speculative draw-pipeline import:** rejected as a direct patch. Its PC evidence is not Android evidence, the GoW III Android configuration disables preparation workers by default, and D3D9/DXVK has a different architecture.
- **Bloodborne unified-memory/KGSL/AOT/affinity import:** rejected. UMA showed memory savings without automatic FPS improvement and includes serious coherence/reset risks; AOT was approximately tied with FEX at a GPU-bound save; fixed affinity is not justified without ProtoDroid thread evidence.
- **Generated/upscaled frames as performance evidence:** rejected. Bloodborne's 29.2 game-FPS result is not 60 real FPS, and GoW III PC/upscaler scenes are not ProtoDroid phone results.
- **ISS as independent confirmation:** rejected. Its Android port is preparation-only, and its async/draw/staging implementations share substantial lineage with the GoW III fork.

## Experimentally rejected candidates

None yet; no optimization experiment has been run in 0.16.
