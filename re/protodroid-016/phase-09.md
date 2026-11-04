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

Native ARM64 run `41800212183` / check `126474212284` confirms the candidate on exact commit `55000220dad9de4e4d97f6759b7fbf9b716b0f28`. Independent recomputation passes **404/404** source-hash, protocol, raw-row, median, ratio, and per-backup checks. Unchanged candidate/baseline medians are **0.075922, 0.111711, 0.085883, and 0.089859**, 12/12 wins at each size, with A/A 0.962197–1.009042. Absolute host savings are 85.55 µs, 133.52 µs, 2.326 ms, and 9.183 ms per unchanged backup. Changed medians are 1.002254, 1.010056, 1.024110, and 0.999639, with A/A 0.980580–1.017097; this is consistent with the measured three-stat overhead and remains inconclusive rather than a changed-path win. The native ARM64 test job passed; the full run's APK job is still pending at this checkpoint.

## H2: direct SHA-1 padding for shader/cache keys

The patched DXVK SHA-1 implementation is used for D3D9 bytecode keys, fixed-function shader keys, and state-cache entry integrity. Its `SHA1Pad` emits `0x80`, then calls `SHA1Update` once per zero byte until the length field, often dozens of calls per hash. Optimized x86-64 assembly retains the loop and external calls. This is a bounded CPU overhead candidate, not a compiler-lock or cache-format change.

The candidate writes the identical one- or two-block padding directly and advances `context->count` by exactly the same padding bit count. It preserves the public `SHA1Pad` contract, including full state, count, and buffer contents. Baseline/candidate differential output matches exactly for **12,128 records / 1,572,405 bytes**, SHA-256 `7e876df2dbe830d22e9394eb4af51866a195abd8823845f7d95e6d19ae7a2264`: FIPS vectors, million-`a`, every length through 2048, seven chunk sizes, and full post-padding contexts through length 1024. Candidate ASan+UBSan and the versioned boundary/chunk/context UBSan test pass with empty diagnostics.

Two balanced x86-64 cohorts (12 and 16 rounds) show candidate/baseline medians of approximately 0.469 at 20 bytes, 0.791–0.795 at 48 bytes, 0.592–0.596 at 128 bytes, 0.705–0.710 at 256 bytes, 0.879–0.935 at 1 KiB, and 0.963 at 4 KiB. A/A is near one and almost all candidate pairs win. The important caveat is absolute materiality: measured savings are only about 27 ns at 48 bytes, 37–169 ns at 1 KiB, and 140–211 ns at 4 KiB. Real Prototype cache-entry counts, bytecode-size distribution, and shader-creation frequency are unavailable, so these isolated ratios do not establish a meaningful loading or frame-time improvement.

**Disposition:** measurement candidate only. `dxvk-zzzzzzzzzzzzzz-sha1-pad.patch`, `sha1_pad_test.cpp`, and `tools/measure_sha1_pad.py` are being submitted to native ARM64 CI for absolute target-ISA savings and patched-build correctness. Adoption requires a defensible materiality judgment; otherwise the patch will be reverted while raw evidence is retained. No SHA hardware feature is assumed and no cache hash/format semantics change.

The source-derived v18 size model compiles the actual patched DXVK headers and accounts for every serialized field. Pipeline-library payloads are `24 × shaderCount` bytes; monolithic payloads are `24 × shaderCount + 74 + 4 × (attributes + bindings + nonzero spec constants)`. DXVK always serializes eight render-target swizzle/blend arrays. Two-shader examples therefore hash 48 bytes for a pipeline library and 122–426 bytes for the modeled monolithic range; the on-disk header and SHA-1 add 24 bytes but are not part of the hashed payload. This is not a real Prototype cache trace. A separate 12-round host cohort at 24/48/122/146/178/266/426 bytes measured candidate deltas of approximately −126/−30/−230/−132/−24/−177/−56 ns per hash. The variation follows padding length (`size mod 64`), reinforcing that entry distribution matters. Model and raw JSONL are under `phase-09/sha1/entry-size-model.json` and `bench-entry-sizes/`.

The first exact H2 native ARM64 run, `41801355490` at `a4c027de773678188ff1024ae4ffcdb522fcc8dc`, **failed in the timing step after one second**. Its patched-source ARM64 correctness test passed, as did the x86_64 CI build and ARM64 dependency build, but no SHA-1 result/check/artifact was produced and downstream ARM64 graphics steps were skipped. Check `126477612796` preserves the job failure. Redirected log retrieval is unavailable, so the next diagnostic revision captures source-extraction, compiler, and driver stderr and publishes an explicit failed check if no result exists. There is no ARM64 timing result or H2 adoption yet.

## Other audited shader/loading leads

- Synchronous cache ingestion has vector/unordered-map growth, but the v18 header has no entry count and entry sizes vary. A file-size-based reserve would be an arbitrary memory/performance tradeoff without a real cache trace; no patch.
- Sequential D3D9 shader modules and downstream pipeline objects already deduplicate. Duplicate `registerShader` requires a concurrent same-bytecode creation race and downstream compilation has compiled-once/state guards; no Prototype occurrence trace justifies changing registration semantics.
- Non-Mali devices do not install the Android pipeline wrapper unless opt-in timing is enabled. Mali's remaining serialization and default-disabled heavy trace are required safety/diagnostic behavior; no lock or worker-count change.
- Per-entry cache flush, queue draining, background warmup, and the 1 KiB entry bound remain intact. No flush batching, format bypass, speculative warmup, or DXVK replacement is proposed.
