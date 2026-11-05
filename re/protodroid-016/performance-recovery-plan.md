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

Validation: concurrent first-use, same-hash collisions, unknown/zero targets, imports, followed thunks, guest context, and publication-order checks pass. A host benchmark uses the actual 126,974 production VAs and 18-bit hash distribution (4 MiB of 16-byte `{VA, function pointer}` slots on 64-bit, load factor 0.484367, mean successful insertion probe 1.343645). Median lookup time improves at every tested working set:

| Working set | Baseline ns/lookup | Candidate ns/lookup | Mechanism speedup |
| ---: | ---: | ---: | ---: |
| 1 | 3.627843 | 2.145136 | 1.69× |
| 8 | 3.713713 | 1.842958 | 2.01× |
| 64 | 3.764751 | 1.963420 | 1.92× |
| 256 | 3.724704 | 1.973314 | 1.89× |
| 4,096 | 3.684405 | 2.073695 | 1.78× |
| 16,384 | 4.767008 | 2.916501 | 1.63× |

Rejected experiments remain rejected: a 64-entry recent-target cache regressed 256/4096-target workloads; 14/15-bit hashes are invalid for 126,974 entries and their small synthetic timing was not production-valid. Packed 8-byte `(VA, table-index)` slots saved 2 MiB but the additional hit-path table read regressed every tested working set, so the 16-byte `{VA, function pointer}` slots remain. An initial full-table benchmark used the wrong synthetic workload and returned all misses (`checksum=0`); it was discarded and rerun with actual VAs/nonzero checksums. These are x86-64 host mechanism results, not ARM64 frame-time or FPS results.

## Recovery iteration 3 — non-distorting frame capture

`frame_interval_trace = 1` previously implied full native detailed diagnostics. It now enables only the existing Present-interval collector and worker-only CSV persistence. Full detailed mode, the diagnostic tour, raw periodic/residual drains, cap/error reporting, final close, and shared log mirroring retain their behavior. Raw-only mode does not enable rect/engine/texture timing or the additional Present-wall timing window; startup records device/source information for later matched reports.

Validation includes 29 structural checks with six detected mutations, UBSan Present/writer/diagnostics/tour tests, runtime bridge tests, 47 rect-lock structural checks, and strict warning-free syntax compilation of the edited production translation units against pinned SDL 2.30.9 and DXVK DirectX headers. This is measurement-path work, not an FPS improvement. It allows the next ordinary-play capture to provide raw p50/p95/p99/max and threshold counts without turning on the heavier diagnostics being investigated.

## Recovery iteration 4 — dense import dispatch

Optimized disassembly showed that every ordinary import called a lazy-static accessor and indexed a 72-byte record containing two strings solely to obtain its function pointer. The candidate publishes a dense `ImportFn` array in lockstep with the existing metadata, including exception rollback. Default dispatch now bounds-checks and calls that array; null-handler diagnostics, registration, D3D9 caller attribution, profiling, `SwitchToFiber`, and context restoration retain the metadata path.

The real-`DispatchImport` host benchmark links optimized production code without UBSan and cycles actual registered thunks. Fourteen measured samples per working set (after warmup and an untimed sample) produced:

| Import-target working set | Baseline ns/import | Candidate ns/import | Mechanism speedup |
| ---: | ---: | ---: | ---: |
| 1 | 5.816045 | 4.603366 | 1.26× |
| 8 | 5.917628 | 4.677889 | 1.27× |
| 64 | 5.799421 | 4.788951 | 1.21× |
| 256 | 5.801734 | 4.724080 | 1.23× |
| 1,024 | 5.838124 | 4.663669 | 1.25× |

Checksums match. The complete UBSan runtime bridge passes with profiling both disabled and enabled; dense publication/bounds checks, 24 caller-profile structural checks with six mutations, the UBSan caller collector, real formatter/analyzer golden path, and 22 analyzer checks pass. This removes measured bridge overhead but is not a D3D9 method-time, frame-time, or FPS multiplier.

## Recovery iteration 5 — trivial guest-context TLS

Cross-translation-unit C++ `thread_local` access emitted weak TLS-init checks around the active guest-context load/store in every import. `tl_ctx` is only a zero-initialized pointer with no destructor, so the candidate uses GNU `__thread` consistently in runtime, imports, threads, and the bridge declaration. It does not force `initial-exec`; a `dlopen` probe has no `STATIC_TLS` flag and passes eight-thread isolation.

Two alternating-order benchmark rounds favor the change at every import working set in aggregate, adding approximately 1.08–1.13× mechanism speedup over dense dispatch alone. Current candidate medians across those rounds are approximately 4.24–4.46 ns/import, versus 5.80–5.92 ns for the original lazy-metadata/C++-TLS baseline. These are host mechanism results with normal run-to-run variation, not FPS evidence.

The production bridge passes with profiling off and on, including eight threads × 2,000 imports per run with exact per-thread active/outer context checks. Strict imports/runtime/thread syntax checks and shared-library TLS isolation pass. Android Clang/ARM64 compilation remains a required CI gate.

## Recovery iteration 6 — DXVK timing guard

Patch 18's disabled `D3D9Timing::Enabled()` still used a function-local static guard on every protected method. The candidate keeps lazy environment evaluation (native sets the variable after DXVK loads) but stores unknown/off/on in a namespace-scope atomic sentinel. Concurrent first calls may repeat the same environment read; relaxed access is sufficient because no associated data is published.

The exact-header disabled benchmark improved from **0.991522 to 0.430360 ns/call**, a 56.6% guard-overhead reduction with identical checksums. This is a small x86-64 host mechanism result, not an FPS estimate. Enabled/disabled UBSan tests now include eight-thread concurrent first use; 82 structural checks with six mutations, four strict translation-unit variants, 24 analyzer checks, and neighboring 56/26/35 pipeline-outcome/drain/warm-cache checks pass. Fresh application of all 20 patches is byte-identical to the tested candidate, with combined diff SHA-256 `8cf88b97768d61f865caef6d7b4c5c854822c38c5e2651c62f4c865477437ec9`.

## Recovery iteration 7 — native ARM64 import-dispatch confirmation

The exact-source fixture added in `f5121a8` registers 1,024 imports, uses eight tagged handlers, warms lazy initialization before timing, checks every timed process's tagged sum and guest-context restoration, and runs balanced baseline/A-A/candidate processes. A deliberate wrong-slot mutation exits with the intended checksum failure. The fixture measures initialized dispatch only; it does not include D3D9 method execution, GPU work, or frame pacing.

Run `42053304301` / neutral check `127335298775` produced two native AArch64 cohorts with 2,000,000 timed calls per process and 24 paired rounds:

| Exact source comparison | Baseline median | Candidate median | Paired candidate/baseline | Wins | A/A median |
| --- | ---: | ---: | ---: | ---: | ---: |
| Pre-dense `e30a2b8` → dense `87d3f2e` | 5.993961 ns/import | 4.728427 ns/import | 0.788690 | 24/24 | 0.999970 |
| Dense `87d3f2e` → current GNU TLS | 4.727398 ns/import | 3.849185 ns/import | 0.814461 | 24/24 | 1.000124 |

The median-derived combined ratio is 0.642177 (about 1.557× mechanism speedup and 2.144776 ns/import saved). This is derived from two separate paired cohorts, not a third combined benchmark, and is not an FPS multiplier. All 144 timed processes returned matching tagged sums. Exact before/after source hashes and the complete paired-ratio arrays are retained under the external `p4-import-dispatch/f5121a8-arm64/` evidence directory.

The CI artifact uploaded successfully, but the sandbox's redirected artifact download failed with EOF; therefore this cohort's raw JSONL is not claimed as independently reopened. Subsequent CI publishes the complete import raw timelines through a separate size-bounded Checks payload as well as the artifact. The same measurement-only update screens the remaining local-static `ImportProfiling()` guard against an atomic relaxed sentinel on ARM64. Its earlier x86-64 result was neutral and the short smoke A/A control was noisy; neither is evidence for adopting that candidate.

## Recovery iteration 8 — ARM64 raw confirmation and rejected profiling-guard candidate

Run `42054113460` / neutral check `127338254611` publishes all three complete raw timelines through the GitHub API. Independent extraction and recomputation passes **702 assertions** over exact source hashes, 216 successful/checksummed processes, per-variant medians/ranges, every paired ratio, and win/loss counts. Raw files and the verification report are under `p4-import-dispatch/f43bf0e-arm64/` in the external evidence store.

| Fixture | Candidate/baseline | Candidate wins/losses | A/A median | Disposition |
| --- | ---: | ---: | ---: | --- |
| Dense dispatch confirmation | 0.789109 | 24/0 | 0.998754 | Retained; second ARM64 cohort confirms the mechanism result |
| GNU TLS confirmation | 0.816613 | 24/0 | 1.000111 | Retained; second ARM64 cohort confirms the mechanism result |
| Atomic relaxed `ImportProfiling()` sentinel | 1.026074 | 0/24 | 0.998935 | **Rejected:** 3.859701 → 3.961780 ns/import, about 2.6% slower |

The rejected candidate's exact source hash is `80d30f0573bfdfc76f6167d881fa2c7e188f937f7edb81c1222920d74d6ab542`; production remains at the retained local-static guard and has no diff from this experiment. The full host control was also excluded from adoption because its A/A cohort contained large scheduling outliers. These are native import-dispatch results, not frame-time or FPS evidence.

## Recovery iteration 9 — DXVK timing guard confirmed on native ARM64

The import-guard regression made an architecture-specific check of the superficially similar DXVK guard necessary. The exact-header fixture uses the pre-P6 `ba19b5a` and current patch headers, initializes the disabled guard before timing, balances baseline/A-A/candidate processes, and checks each result against the same arithmetic without a guard. An always-enabled mutation is detected. The baseline and candidate header hashes remain `3c6c8f62a5cae99f22e5c56533396c392ff5cca3fbf442682dfb0f67dd40d352` and `17e72205218bd91c2d0f4836b1db24763662d816e6b40b713eaecc92a5076c8a`.

Run `42056747768` / neutral check `127347677468` produced **24/24 candidate wins**, paired median ratio **0.749980**, and A/A **0.999975**. Whole-fixture medians were 47.163171 → 35.3709085 ms over 20 million calls, or **2.358159 → 1.768545 ns/call**, saving **0.589613 ns/call**. This is a whole-function guard-fixture result, not the x86 guard-only 56.6% metric and not an FPS estimate. Independent extraction/recomputation passes **236 assertions** over all 72 raw processes, exact patch/header hashes, checksums, medians/ranges, every ratio, and win counts. Raw evidence is under `p6-dxvk-timing-guard/d5a0aac-arm64/`; P6 is retained.

The first attempt at this fixture (`42055953358`) exited 128 before benchmarking because its shallow checkout lacked the baseline revision. A local shallow-clone reproduction matched the failure; `d5a0aac` adds the required fetch. No timing result from that failed attempt is used.

## Recovery iteration 10 — source-built candidate APK and remaining device gate

Run `42053304301` on `f5121a8e381eec862e18a8b89961c019609c3638` completed successfully, including source-built Android ARM64 `libmain.so`, APK package/sign, and APK/embedded-DXVK verification. The candidate is artifact **`13430555528`**, `prototype-apk`, with server-reported ZIP size 57,993,323 bytes and SHA-256 `995c9857a975a6cf45d7484d6dfb08605d24625a0c0e8ecdcf9f2918880e0933`. Its browser URL is `https://github.com/Nazatric/ProtoDroid/actions/runs/42053304301/artifacts/13430555528`.

A local artifact download failed with EOF at the redirected GitHub blob host, so the digest is ZIP metadata rather than an independently reopened inner-APK hash. No installation or physical-device result is claimed. Later `f43bf0e`, `7f45d90`, and `d5a0aac` commits change only CI/workflow, measurement tools, and research documentation; their runtime code is identical to the candidate reference. Confirmation runs `42054113460` (`f43bf0e`) and `42056747768` (`d5a0aac`) also completed successfully with source-built APK/embedded-DXVK verification. No prebuilt-native fallback, merge, tag, or release was used.

## Next device-capture contract

For the first candidate build, use ordinary Play with `frame_interval_trace = 1` and leave `detailed_diagnostics`, `d3d9_timing`, `pipeline_timing`, `pipeline_trace`, `import_profile`, `guest_function_trace`, `warm_cache_trace`, and the diagnostic tour off. Keep resolution, quality, dynamic shadows, anisotropy, AA, FIFO/swapchain settings, drivers, audio, mods, controls, saves, and the scene/route unchanged between A/B runs. Do not use frame generation, upscaling, or an FPS unlock as a performance result.

Run a separate warm-up before a measured warm run, then play the same ordinary route for at least 60 seconds and exit cleanly so the CSV close trailer is written. Preserve `proto-frame-intervals.csv`, `proto-native.log`, `app_process64_d3d9.log`, the active INI, and a visual-completeness note. The logs must identify the exact source/device/GPU/configuration and whether cache recovery restored a populated candidate or found none. Cold-cache/stall attribution is a separate run and must not be mixed into the warm baseline.

The supplied files are inputs to `analyze_frame_intervals.py`, `perf_evidence.py summarize`, and `perf_evidence.py compare-raw`; scene and cache-state labels must match. A candidate claim requires exact raw p50/p95/p99/max, threshold rates, and visual correctness. One run is still not significance proof.

## Current non-completion statement

Work is **not finished**. The newest evidence establishes a cold-cache run and an observability/startup-order weakness, not a complete explanation for every low-FPS report. No default-path root cause, matched FPS recovery, visual verification, 60+ FPS result, 3× result, or stutter elimination has been established. PR #40 remains open and unmerged; no tag or release is authorized.

## Recovery iteration 11 — post-`28a7e56` device evidence and native frame-thread roster

Artifact `13443418586` / run `42082904844` was source-built and CI-valid, but the user reports that low-30s FPS persists. It is therefore a rejected device-performance candidate, not an accepted fix. Logs commit `865933c4064d38d8124cd795267e467ed2dd2cfb` identifies the primary run as `28a7e56`; its previous native log belongs to `f5121a8`, so the two files are not a controlled same-build A/B. The primary ordinary run has detailed/raw metrics disabled and cannot establish a new CPU/GPU/present root cause.

The older detailed diagnostic-tour capture remains useful for mechanism selection, not ordinary-play FPS acceptance. Its per-thread samples show the D3D9/render thread and `dxvk-cs` among the largest CPU consumers. The current ADPF roster discovers guest priority threads and the observed Present thread, but native DXVK CS/submission threads never pass through guest `SetThreadPriority`. The next candidate scans `/proc/self/task` once before session creation, accepts only exact `dxvk-cs` and `dxvk-submit` names, deduplicates them with the existing roster, and retains stale-TID pruning and session fallback. Pipeline/compiler workers, near-name matches, CPU affinity, and host priorities are unchanged. Device logs must confirm the resulting roster; this is not yet an FPS claim.

The same batch makes naturally aligned TSO accesses the fall-through path using compiler branch expectations. All acquire/release instructions and all unaligned plain-access/fence fallbacks remain. A generated-source census has 226,773 static `R32` and 144,025 static `W32` sites, so hot layout matters more than another isolated host-table nanosecond. A cold-helper prototype was rejected before integration: calls changed register allocation and increased the sample object's text from 33,002 to 37,706 bytes (34,906 with `preserve_all`). The hint-only AArch64 sample instead makes aligned `LDAPR` fall through, retains the unaligned `LDR + DMB`, and has 32,994 bytes of text versus 33,002 baseline. Native x86-64 UBSan and source checks pass; all 16/32/64-bit aligned and unaligned values and store-bound guards are covered in the runtime bridge, which also runs on native AArch64 CI. An exact-header native-AArch64 CI fixture now compares the pre-change and candidate headers with opaque aligned addresses, exact checksums, 24 balanced rounds, and an A/A control; its raw timeline is published through Checks and the measurement artifact. Adoption still requires a material, non-regressing mechanism result there and subsequent device evidence.

For a separate opt-in diagnostic run, `detailed_diagnostics = 1` now also enables the existing D3D9 method/lock timing by default. `d3d9_timing = 0` can explicitly disable it, and ordinary Play still has no method-timing clocks/counters. This is intended to distinguish device-lock wait from actual draw/state/query work; the instrumented run is not a steady-FPS baseline. No device FPS improvement, stable 60/90 result, or final 0.16 acceptance is claimed by this iteration.

## Recovery iteration 12 — TSO layout rejection and DXVK ThinLTO build candidate

Source `5dd945f52db7062f4fd07b6c1932bfed60b17c21` / run `42095927749` published native-AArch64 TSO check `127487191578`. Independent extraction recomputed **530 assertions** over all 72 successful checksummed processes, exact header hashes, per-variant statistics, every paired ratio, and win/loss counts. Baseline/A-A/candidate median elapsed values were 12,004,077.5 / 12,005,010 / 12,002,522 ns for four ordered loads per iteration. Candidate/baseline paired median ratio was **0.9993754168391** with 16 wins and 8 losses, but A/A was **1.000441285902971** with comparable movement. This is immaterial, not an FPS result, and fails the stated adoption gate. The branch-expectation layout is therefore reverted and its active CI fixture is removed; the broader aligned/unaligned runtime correctness coverage remains. Raw JSONL, Check response, recomputation, and hashes are under the external `performance-recovery/tso-layout/5dd945f-arm64/` evidence directory. The same run's `bink-tests` job also completed with failure. A local run of its suite reproduced a stale structural assertion that still required `d3d9_timing` to default off in detailed mode; redirected hosted-log retrieval then failed with EOF, so this is not relabeled as an independently reopened hosted-cause proof. The corrected contract explicitly requires ordinary-Play-off/detailed-on with an always-on negative control; all 82 structural checks, six mutations, enabled/disabled UBSan tests, and four strict translation units pass locally. The red job is retained, and source `5dd945f` is not accepted as an APK/device candidate.

The next independent candidate optimizes the existing DXVK v2.5.3 protected D3D9 submission paths with **ThinLTO** across their existing internal static-library and translation-unit boundaries. The historical render capture makes this more relevant than further import-dispatch nanoseconds: one 10-second window attributed about 30% to 714,505 `DrawIndexedPrimitive` calls, and the device was created with `D3DCREATE_MULTITHREADED`. Only the DXVK Meson configuration changes: `-Db_lto=true -Db_lto_mode=thin`, plus a fail-closed check that `-flto=thin` reaches the effective NDK commands. The pinned source, all 20 patches, generic ARMv8-A dependency target, ARMv8.0 compatibility variant, public D3D9 version script, graphics, cache format, drivers, and presentation remain unchanged. Fresh application of all patches still has combined diff SHA-256 `8cf88b97768d61f865caef6d7b4c5c854822c38c5e2651c62f4c865477437ec9`; six build-policy mutations are now detected.

The first ThinLTO run did not produce an acceptable dependency/APK. In run `42096757241` / issue comment `6667686292`, DXVK completed all 214 compile/link actions, then failed the required `DXVK warm-cache drain:` packaged-library marker. The method is deliberately uncalled until a real loading boundary is established, so ThinLTO removed the otherwise unreachable chain; the check was not bypassed. Standalone dependency run `42096757112` also failed its ARM64 and x86-64 build jobs. The integration fix roots the exact existing `_ZN4dxvk10DxvkDevice16waitForWarmCacheEm` symbol with linker `-u`, adds no caller or public ABI, and requires `llvm-nm` to verify the final entry point in addition to every existing string marker. A local Zig/LLVM ThinLTO/version-script probe retained the rooted method and marker while still removing an unrelated dead method; baseline, unrooted, and rooted binaries/sources are under external `performance-recovery/lto-retention-probe/`. The next CI run must establish actual Android compilation, complete symbol/marker retention, and APK packaging. No draw-time or device-FPS improvement is claimed before that build and a matched device test.
