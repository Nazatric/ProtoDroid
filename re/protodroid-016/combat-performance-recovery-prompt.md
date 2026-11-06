# Self-directed prompt — opening military-combat performance recovery

Status: **active execution prompt**, created after artifact `13449902337` improved general FPS but still dropped to **30–35 FPS in the opening fight against military personnel**. This supplements the preserved 0.16 recovery plan; it does not relax any earlier requirement.

## Mission

Continue autonomously until the exact opening military-combat sequence is smooth on the user's device. The numeric acceptance target remains **real rendered gameplay at 60+ FPS with no dips below 60**, materially better p99 frame time, and no new stutter. Stable 90 FPS remains an ambition, not a claim. Preserve all graphics, effects, dynamic shadows, content, controls, mods, audio, saves, drivers, ARM64 compatibility, and package behavior.

Do not count frame generation, LSFG output, skipped frames, upscaling, reduced quality, disabled effects, cosmetic counters, menu/movie FPS, or an average that hides combat dips as success. Do not merge, tag, release, or substitute an older/rejected APK.

## Evidence boundary

1. The tested source is `72a5c7ab9896478a723b7a4e72872ae320af67ab`; artifact `13449902337` is the latest generally improved but combat-rejected candidate.
2. Logs revision `6cf00687e8aac94e9d8a914bd7c597288c980652` identifies that exact source, Mali-G720 MC7, 2400×1080, quality/shadow 2, anisotropy 2, FIFO, and a restored 683-entry DXVK cache. Ordinary detailed/raw metrics are disabled, so it does **not** identify the combat bottleneck or provide a p99.
3. That run discovers `dxvk-cs` and `dxvk-submit`, but a later dynamic ADPF thread update is rejected. Treat this as a concrete session-roster defect to fix and measure, not proof that ADPF explains the whole 30–35 FPS result.
4. Logs revision `449a16e24d90f3df19d6357b6db33f49c8f17f81` adds raw intervals, CPU/import/D3D9/pipeline evidence for exact source `72a5c7a`, but it is a cold, instrumented, 4×-guest-clock diagnostic tour that ends at `wall_grab_climb`. It never reaches combat and is not acceptance data. Its inclusive/nested timings must not be summed into a frame-time total.
5. Previous rejected builds and mismatched previous/current logs are not controlled A/B evidence. CI, binary size, host timing, and native microbenchmarks are mechanism evidence only.
6. Keep supplied logs and originals read-only. Export analysis copies outside Git with source revision and SHA-256 provenance.

## Execution loop

### Phase 15 — combat evidence and session correctness

- Record the exact new-log identities and limitations in the recovery plan.
- Make ADPF dynamic updates prune stale TIDs before every `APerformanceHint_setThreads` call, retain the exact DXVK-thread allowlist, expose the Android status and pruning count, and test successful, rejected, dead-thread, duplicate, and fallback behavior.
- Do not add per-frame `/proc` scans, change CPU affinity, or register compiler/pipeline workers as frame threads.
- Preserve raw-only and detailed opt-in capture. A future same-build combat capture must include raw frame intervals and, separately, detailed CPU/D3D9/lock attribution; neither instrumented run is an unmodified FPS baseline.

**Gate:** host/UBSan and source-policy tests pass; the ordinary path retains bounded registration-only work. Device acceptance remains open.

### Phase 16 — generated-code experiments and cross-part ThinLTO candidate

- **Rejected tail-source cleanup:** removing the apparent post-tail reload/final spill reduced generated C but not native code. The baseline already emitted the same `mov` + `jmp`; 24 paired x86-64 rounds gave candidate/baseline **1.000286** versus A/A **1.004307**. All 189 generated-file changes and the generator edit were reverted. Do not resubmit this as an FPS optimization.
- **Rejected same-file interposition flag:** real generated same-part code already inlined at `-O2`; `-fno-semantic-interposition` alone did not change the instructions. It has value only as part of the cross-part LTO policy.
- **Active candidate:** compile generated C with ThinLTO and keep native C++ non-LTO. Android already uses `-Bsymbolic` and exports guest symbols, so cross-part inlining must retain every out-of-line `f_*` entry. `RECOMP_LTO=0` is the opt-out; unsupported modes fail closed.
- The generated census contains **202,741 ordinary direct cross-part calls**. In the real `part_0070`/`part_0061` pair, all 787 symbols survived and cross-part `bl` sites fell from 4,091 to 2,122, while pair `.text` grew about 9.3%. This is a speed/code-size tradeoff, not a guaranteed device win.
- The synthetic cross-part runtime/context check passes; 24 paired x86-64 rounds gave **0.726803** candidate/baseline versus **0.972973** A/A. Require the native ARM64 run, both Android variants, full dynamic-symbol inventory, existing hot replacement/regressions, and exact NDK flags. Do not translate the microbenchmark ratio into FPS.

**Gate:** exact source-built ARM64 APK with preserved function/hook inventories and broad runtime regressions; same-scene device FPS remains a separate gate. Revert if the build is unreliable, correctness changes, or the device tradeoff is unfavorable.

### Phase 17 — scene-specific attribution and further optimization

- Use the new candidate in the opening fight, not only menu/general traversal. Keep settings, save/route, cache warmth, and driver matched against `13449902337`.
- If combat still dips, rank the next mechanism from same-build raw/detailed evidence: guest simulation/AI and animation, D3D9 state/draw/query volume, DXVK CS/submission, lock/wait time, streaming/audio, pipeline creation, or GPU/present limits.
- Prefer substantive recompiler/codegen, native bridge, DXVK, Vulkan, scheduling, and cache optimizations. Use pinned comparable projects and upstream source where useful; import only semantics-compatible mechanisms.
- Run balanced mechanism A/B experiments where possible, retain negative results, and avoid repeating rejected TSO, profiling, unsafe synchronization, quality-reduction, or unrooted-LTO experiments.

**Gate:** no root-cause assertion without evidence. No FPS improvement claim without measured frame-time evidence. Green CI alone never closes this phase.

### Phase 18 — delivery and repetition

- Batch substantive source changes, commit/push only to `arena/5eb1c06c-protodroid`, and monitor the exact run through source `libmain.so`, package/sign, and artifact creation.
- Deliver only a new downloadable source-built APK with its exact run/artifact/source identity. State what was verified and what remains unmeasured.
- **Current test candidate:** source `888a1673421f7e56303bd492b834ddf1626dc2e9`, run `42170984244`, `prototype-apk` artifact `13483441501`. Full CI/package gates passed; opening combat, p99, visuals, and device FPS remain unmeasured. The ARM64 raw timing artifact could not be reopened, so no ARM64 timing ratio is claimed.
- After feedback, fetch the new Logs revision read-only and repeat the loop. Do not stop at a promising startup log, a smaller binary, or a generally improved FPS report while opening combat remains below target.

## Current non-completion statement

The opening military-combat sequence remains below acceptance. No same-build combat p99, stable-60/90 result, GPU/CPU root-cause isolation, or accepted replacement APK exists yet. This prompt authorizes continued engineering and measurement; it is not itself evidence of improvement.
