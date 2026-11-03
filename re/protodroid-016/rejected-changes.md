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

## Experimentally rejected candidates

None yet; Phase 1 is baseline establishment, not optimization.
