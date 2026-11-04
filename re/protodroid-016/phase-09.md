# Phase 9 — Shader compilation, loading, and cache behavior

Status: **active**. Started **2026-11-04T08:13:48Z** from completion head `a2a4d4843143d6bc77482344f2479b187ac59754`; final Phase 8 production source is `9a65eb9b59dc1e755d434ae9204affef547ef5d4`. Phase 8 is complete; Phase 10 remains blocked until Phase 9 completes.

## Scope and evidence limits

Investigate first-use shader/pipeline creation, state-cache lookup/write/read behavior, startup migration/backup, cache concurrency, and driver-specific compile serialization. Preserve the patched DXVK 2.5.3 base, Mali compile lock, cache durability/fallbacks, graphics features, and diagnostic-tour isolation. No physical Android device, ADB, GPU profiler, real game shader trace, cold/warm device startup, or phone frame-time data is available. Host and native-CI results will not be presented as device FPS or shader-stall improvements.

The reference DXVK checkout remains clean at `c707d9026f33b6ab89639f154b6ac5f6326fa037`. A separate Phase 9 copy has the repository's full ordered `dxvk-*.patch` series applied; patch and combined-diff hashes are under `phase-09/` external evidence.

## Source-led cache/path findings

The fully patched source already has a dedicated state-cache writer thread, per-entry `flush()` for Android kill resilience, stop-time queue draining, a background cache-pipeline warmup worker, and opt-in pipeline driver/wait timing. The Mali wrapper serializes only the actual Vulkan pipeline-creation boundary and must remain. No flush removal, imaginary warmup, driver-lock removal, cache-version bypass, or DXVK replacement is proposed.

Ordinary Play schedules `MirrorCopyDXVKLogsAsync()` every 30 seconds. Its helper thread calls `BackupStateCache`; after the initial populated-cache check, every call copies the entire source through a 16 KiB buffer and atomically replaces the shared backup, even when source and target are unchanged. This is off the engine thread in the normal path, so the hypothesis is reduced redundant I/O and storage contention—not a claimed frame-time win.

### H1: skip only a provably unchanged successful backup

- **Baseline:** `9a65eb9b59dc1e755d434ae9204affef547ef5d4` production / `a2a4d4843143d6bc77482344f2479b187ac59754` research head; `state_cache.cpp`, `state_cache.h`, and the mirror caller in `main.cpp`.
- **Candidate constraint:** an explicit caller-owned tracker may skip only when source and target device/inode/size/mtime/ctime identities all match those recorded after a stable successful copy. Any source edit/append/replacement, target edit/replacement/wipe, path change, failed copy, or source metadata instability before/after copying must force a full copy or retry.
- **Preservation:** first backup always copies; changed caches still copy in full; missing/empty/invalid sources never destroy the last good backup; external target wipes are restored; temporary rename and no-fsync backup policy remain unchanged; diagnostic-tour isolation and default untracked callers retain original behavior.
- **Measurement plan:** exact-source normal/UBSan differential tests, target-inode proof of skip versus copy, same-size rewrites, external target edits/wipes, unstable-source reasoning, and balanced unchanged-backup timing at multiple cache sizes. The 30-second schedule is exact; actual device cache sizes, shared-storage latency, FPS, and shader stalls remain unmeasured.
