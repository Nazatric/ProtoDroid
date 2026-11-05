# ProtoDroid 0.16 performance recovery — execution brief and roadmap

Status: **active; release-blocking; no FPS recovery or stutter elimination established yet**. This plan continues the ordinary-Play investigation after the reports of sub-40 FPS and drops into the 10–28 FPS range. It does not replace the preserved runtime, graphics, mod, controls, launcher, ABI, package, save, or diagnostic-tour requirements.

## Improved execution brief

> Autonomously investigate and recover ProtoDroid 0.16 ordinary-Play performance. Treat the reported low FPS and severe drops as release-blocking. Preserve the existing feature and visual contract: no fake FPS, generated-frame inflation, frame skipping, hidden resolution/quality reductions, disabled effects, cosmetic counters, or LSFG presented as engine performance. Establish the exact tested source, settings, cache state, route, thermal state, and raw frame/present evidence before claiming a regression or improvement.
>
> First isolate overhead introduced on the default path, including generated guest instrumentation, import/caller profiling, logging, diagnostics, synchronization, and cache maintenance. Preserve unexpected worktree changes and make candidates independently revertible. Separately attack cold/warm shader-pipeline behavior: cache recovery and durability, queue/compile timing, first-use stalls, and driver-safe compilation concurrency. Do not confuse queue draining with elimination of compilation work.
>
> Profile before optimizing broad CPU, rendering, audio, streaming, or synchronization paths. Use host/native tests for correctness and microarchitectural screening, CI for exact ARM64 source builds, and matched physical-device A/B runs for FPS and frame-time claims. A green build is not completion. Continue research into pinned native recompilation, DXVK, driver, and mobile-renderer implementations, importing only mechanisms compatible with ProtoDroid's preserved renderer and feature set.
>
> Deliver concrete commits, tests, CI checks, source-built APKs, evidence manifests, negative results, and next priorities. Keep large/raw artifacts outside Git with provenance and hashes. Commit and push only to the authorized working branch. Do not merge, tag, or publish a release without separate authorization. The user's performance goals—60+ FPS on capable devices, potentially up to 3× in limited hot paths, and removal of shader/other stutters—remain goals, not assertions, until comparable device evidence supports the specific claim.

## Evidence that changes priority

| Observation | Exact evidence | Engineering consequence |
| --- | --- | --- |
| Newest ordinary-Play run is cold-cache | Attached DXVK log: `No state cache file found`, followed by `Creating new state cache file` | Cold shader/pipeline work is a mandatory suspect for stutter; do not compare this run directly with a warm-cache run |
| Earlier run of the **same source** was warm | Source `98c306fa017468faa08ebaa8f92984eef46cb332`; DXVK read 1,037 valid entries | The binary alone does not explain the cache-state difference; recovery, persistence, and run conditions must be audited |
| A usable shared backup exists in the earlier upload | 180,832-byte version-18 cache; SHA-256 `49f04fb3b61a660ea3a5c5af7a5ae49eb8076cf181119ab20788a3e01baec511` | If still present on the device, ordinary startup should select it; visibility into candidate selection and copy failure is required |
| Recovery result is absent from the persistent native log | `PlanStateCacheMigration` / `MigrateStateCache` currently run before `MirrorInit` and before `g_log` opens | Move recovery after mirror initialization and before D3D9 initialization; log before/source/result/after state without adding gameplay work |
| Newest logs still lack detailed timing/raw intervals | `detailed_diagnostics = 0`, no Presented-frame windows or `proto-frame-intervals.csv` | The logs corroborate cache state and settings, not an independently measured FPS value; continue source work without inventing numbers |
| Resolution/features remain intact | 2400×1080, quality/shadow 2, anisotropy 2, FIFO interval 1, six swapchain images | Do not “recover” performance by silently weakening this configuration |

Newest attachment identities are recorded under `/home/user/protodroid-016-evidence/device-logs/latest-attached-20261105/`. The supplied originals remain read-only.

## Ordered workstreams and gates

### P0 — Cache continuity and startup evidence (active)

1. Preserve populated app-private caches and existing shared/legacy recovery semantics.
2. Run ordinary recovery after shared mirror initialization, while still preceding D3D9/DXVK creation.
3. Persist target status, selected source, migration result/error, and final status in `proto-native.log`.
4. Exercise missing, empty, populated, invalid, legacy, shared-copy, failed-copy, and backup-preservation cases.
5. On device, verify that a populated shared cache is restored before DXVK reports reading it. A missing candidate must be reported as such, not described as a successful repair.

**Exit gate:** exact startup logs and matched cold/warm runs establish whether recovery works. No FPS claim is required or permitted from this startup-only change.

### P1 — Default-path regression isolation (active)

Audit `6cf024f828a8f47796ff3a3050a321c272883239..ba19b5a32c186e0e5fe39a6a3c63fa92c1b9dd86` and its generated-source inputs. Build a per-change ledger covering:

- `RecompGuestFunctionEnter/Exit` generation and call sites;
- native import/caller attribution and lock/allocation behavior;
- Present/frame tracing, D3D9 timing, pipeline timing, and warm-cache sampling;
- DXVK stats/pipeline calls, logging, worker synchronization, and cache I/O;
- compiler/codegen/layout changes that can affect hot guest functions even with diagnostics nominally off.

Candidates must be measured against a comparable baseline or rejected. Disabled diagnostics must have near-zero steady-state cost; any unconditional entry/exit probe is a regression candidate, not a harmless no-op by assertion.

**Exit gate:** identified default-path costs are removed or bounded, with differential generated-source checks, tests, and before/after timing at the appropriate layer.

### P2 — Guest/recompiler hot paths

Use exact call counts, import callers, function timing, host samples, and native ARM64 disassembly to rank guest costs. Prefer generated-code/layout improvements, redundant helper elimination, and bounded memoization with proven invalidation semantics. Preserve PPC64 semantics, unaligned access behavior, memory ordering, exceptions, and mod entry points. Do not copy another project's game-specific renderer or reduced-content workload into ProtoDroid.

**Exit gate:** ARM64 correctness plus measured CPU-side improvement in a representative workload; physical FPS remains a separate device gate.

### P3 — Shader/pipeline stalls and driver concurrency

Separate three different costs: DXVK state-cache lookup, SPIR-V/shader-module work, and actual Vulkan pipeline creation. Record queue depth and active/completed work without blocking pipeline compilation or making unsupported queue assumptions. Retain Mali serialization/Turnip capability guards. Investigate loading-screen reconstruction only where the existing cache contains complete valid state and the lifetime is safe.

**Exit gate:** matched device captures show reduced first-use frame-time tails/stall counts without rendering loss, instability, or merely moving the same stall elsewhere.

### P4 — Rendering, presentation, streaming, and audio

Profile GPU/CPU/present wait separately at the preserved 2400×1080 settings. Audit redundant state, uploads, render-target barriers, synchronization, streaming file I/O, Bink/FAudio work, and present pacing only when evidence ranks them. No hidden upscaling, LOD/effect removal, frame skipping, or menu/gameplay conflation.

**Exit gate:** scene-specific frame-time and visual/correctness evidence, not a synthetic average alone.

### P5 — Matched physical-device verification

Required A/B controls: exact APK/source hash, same device/driver and settings, same save/route/input, matched cold or warm cache state, comparable thermal/battery conditions, and sufficient repetitions. Capture native/DXVK logs, raw frame intervals, detailed timing, and cache-entry/queue evidence where applicable. Report median and tail frame times, Presented-frame FPS, loading/shader stalls, and failures/rejected samples.

**Exit gate for 60+/3×/stutter claims:** the exact claimed metric improves by the claimed amount in matched device evidence. CI, host benchmarks, another repository, and a single warm run cannot substitute for it.

### P6 — Research, integration, and delivery

Continue pinned-source research (including Skate 3/Buku313, rexglue, DXVK, cache/pipeline reconstruction, drivers, SDL/FAudio, and mobile renderers). Record exact revisions and why each mechanism does or does not transfer. Batch justified source changes; run broad regression and exact CI; independently verify downloadable source-built APK artifacts when possible; then iterate on device evidence.

## Recovery iteration 1 — source candidates and mechanism evidence

| Candidate | Exact validation | Boundary |
| --- | --- | --- |
| Cache recovery after mirror initialization, with persistent before/source/result/after reporting (`c180276`) | Fresh DXVK v2.5.3 + all 20 patches; cache suite passes; real 180,832-byte/1,038-entry shared backup restores missing → populated with identical bytes and retained source; startup-order report checks pass | Startup host correctness only. It does not prove the device had a candidate, that Android shared-storage recovery now succeeds, or any FPS/stall change |
| Compile generation-selected guest probes only for an explicit `GUEST_FUNCTION_TRACE=1` investigation build | Ordinary C object has no probe references; instrumented object retains both; enabled runtime/analyzer, collector, structural/mutation, and real-DLL differential tests pass. Real generation: 3,593 functions, zero unsupported instructions | No gameplay or rendering semantics change. The ordinary APK no longer promises guest records merely from the INI switch; the startup log explicitly identifies that mismatch |
| Disabled guest-probe mechanism benchmark | Same arithmetic/control, 21 interleaved samples of 1,000,000 pairs: checked-in runtime median 3.092231 ns/pair; compile-out candidate 0.004234 ns/pair; checksums identical | x86-64 host screening only, not ARM64 device cost, function-call frequency, frame time, or FPS. This small cost alone does not explain the reported regression |

The preserved local edits that remove the same three generated probes remain uncommitted and untouched. The production candidate instead removes their calls during ordinary preprocessing, so clean generated-source verification and optional instrumented generation remain intact.

## Recovery iteration 2 — computed guest dispatch

The generated inventory contains 21,904 computed `RecompDispatch` sites. Disassembly showed that every `RecompLookup` still entered generic `std::call_once`/`pthread_once` and its TLS setup after the immutable hash had been built. The candidate builds the complete table, release-publishes its pointer, and uses an acquire/null fast path; native startup initializes it explicitly, while a synchronized lazy fallback remains for tests and early embedders.

Validation: concurrent first-use, same-hash collisions, unknown/zero targets, imports, followed thunks, guest context, and publication-order checks pass. A host benchmark uses the actual 126,974 production VAs and 18-bit hash distribution (2 MiB table, load factor 0.484367, mean successful insertion probe 1.343645). Median lookup time improves at every tested working set:

| Working set | Baseline ns/lookup | Candidate ns/lookup | Mechanism speedup |
| ---: | ---: | ---: | ---: |
| 1 | 3.627843 | 2.145136 | 1.69× |
| 8 | 3.713713 | 1.842958 | 2.01× |
| 64 | 3.764751 | 1.963420 | 1.92× |
| 256 | 3.724704 | 1.973314 | 1.89× |
| 4,096 | 3.684405 | 2.073695 | 1.78× |
| 16,384 | 4.767008 | 2.916501 | 1.63× |

Rejected experiments remain rejected: a 64-entry recent-target cache regressed 256/4096-target workloads; 14/15-bit hashes are invalid for 126,974 entries and their small synthetic timing was not production-valid. An initial full-table benchmark used the wrong synthetic workload and returned all misses (`checksum=0`); it was discarded and rerun with actual VAs/nonzero checksums. These are x86-64 host mechanism results, not ARM64 frame-time or FPS results.

## Current non-completion statement

Work is **not finished**. The newest evidence establishes a cold-cache run and an observability/startup-order weakness, not a complete explanation for every low-FPS report. No default-path root cause, matched FPS recovery, visual verification, 60+ FPS result, 3× result, or stutter elimination has been established. PR #40 remains open and unmerged; no tag or release is authorized.
