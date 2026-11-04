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
- **Candidate constraint:** an explicit caller-owned tracker may skip only when source and target device/inode/size/mtime/ctime identities all match those recorded after a stable successful copy **and the two files are byte-for-byte equal**. Any source edit/append/replacement, target edit/replacement/wipe, path change, failed copy, or source metadata instability before/after copying must force a full copy or retry.
- **Preservation:** first backup always copies; changed caches still copy in full; missing/empty/invalid sources never destroy the last good backup; external target wipes are restored; temporary rename and no-fsync backup policy remain unchanged; diagnostic-tour isolation and default untracked callers retain original behavior.
- **Measurement plan:** exact-source normal/UBSan differential tests, target-inode proof of skip versus copy, same-size rewrites, external target edits/wipes, unstable-source reasoning, and balanced unchanged-backup timing at multiple cache sizes. The 30-second schedule is exact; actual device cache sizes, shared-storage latency, FPS, and shader stalls remain unmeasured.

### H1 implementation, rejected shortcut, and adoption candidate

The first metadata-only tracker **failed** the concurrent differential stress harness: 8 failures in 3,304 checks, with seven `content-mismatch` diagnostics reproduced at iteration 161. A same-size external target edit inside one filesystem timestamp tick left the recorded identities apparently unchanged. That shortcut is rejected and preserved in `tracked-backup-stress.json` and `tracked-backup-stress-debug.{json,stderr}`; metadata alone is not an acceptable skip condition.

The final candidate compares both complete files before every tracked skip. The two 16 KiB comparison buffers use `nothrow` heap storage so the crash-requested mirror path does not gain a 32 KiB stack frame; allocation failure falls back to the legacy full copy. The ordinary mirror owns one tracker for its one cache source/target pair under the existing mirror mutex. Two-argument callers retain unconditional-copy behavior. Changed copies reuse the successful source probe but retain source before/after stability validation and atomic replacement.

Final expanded UBSan tests pass with empty build/runtime diagnostics. They include a deterministic identity-collision case that fails without byte comparison, ctime-only source detection, append, source/target path changes, target edit/wipe, invalid-source preservation, failed-copy retry, and untracked compatibility. Final concurrent cohorts also pass with empty diagnostics: normal **16,504 checks / 0 failures**, ASan+UBSan **4,952 / 0**, and TSan **2,476 / 0**, with digests `80835f6810778d48`, `b3de759621644e38`, and `b1170e98aa78810c` respectively.

The versioned `tools/measure_state_cache_backup.py` fixture compiles the pinned baseline, identical A/A control, and exact candidate as separate processes with balanced order rotation. Final x86-64 host unchanged-backup results are:

| Cache bytes | Candidate/baseline paired median | Wins/losses | A/A median | Baseline ms/backup | Candidate ms/backup |
|---:|---:|---:|---:|---:|---:|
| 4,096 | 0.0602347816 | 12/0 | 1.1038778880 | 0.082831 | 0.004925 |
| 65,536 | 0.0665859136 | 12/0 | 1.0110155670 | 0.170345 | 0.011544 |
| 1,048,576 | 0.1024109122 | 12/0 | 0.9874920013 | 1.320025 | 0.134678 |
| 4,194,304 | 0.1193188578 | 12/0 | 1.0650615659 | 5.832806 | 0.610081 |

This is a robust host reduction in redundant write/rename work, not an Android FPS or shared-FUSE measurement. Changed-source timing is **inconclusive**, with paired medians 1.137217, 0.926102, 0.980736, and 1.075734 from smallest to largest; the corresponding A/A medians are 1.023074, 1.072313, 0.998769, and 1.015926. In particular, independent median totals at 4 MiB are dominated by host write spikes and are not presented as a candidate regression or win. A linked syscall-count fixture isolates the changed path to exactly **three additional `stat` calls**, with identical read/write byte counts, opens, temporary-file creation, flush, and rename behavior to baseline. The unchanged candidate performs zero writes, zero temporary files, and zero renames while still reading and comparing both files.

**Disposition:** adopt the byte-validated tracker for exact-revision native ARM64 and Android CI, with no changed-path performance claim and no physical-device FPS, frame-time, pacing, shader-stall, or sustained-thermal claim. Raw JSONL, source/binary hashes, all rejected candidates, fixture failures, stress logs, and timing records are under `/home/user/protodroid-016-evidence/phase-09/`. Native ARM64 CI publishes the same two modes and compact raw timing through the neutral `arm64-state-cache-evidence` check.
