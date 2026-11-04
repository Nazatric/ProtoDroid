# Phase 8 — D3D9 bridge, DXVK submission, and translation overhead

Status: **active**. Started **2026-11-04T05:47:41Z** from Phase 7 completion commit `c03364950239b1d7d8a34741fe2c73b92df7258b` on `arena/5eb1c06c-protodroid`. No Phase 8 runtime change or performance result has been adopted.

## Baseline and available tools

- Baseline `src/native/d3d9.cpp` is copied read-only to `/home/user/protodroid-016-evidence/phase-08/baseline-d3d9.cpp` with a SHA-256 sidecar. Baseline source is exactly Git commit `c033649`.
- The bridge uses fixed-chunk atomic host-object lookup. The generated include has 249 direct `Host<...>(Arg(...))` forwarding expressions and 16 `Unwrap<...>(Arg(...))` expressions; this is static syntactic reachability, not a real-game call frequency.
- There is no attached Android device, ADB, Vulkan profiler, simpleperf/Perfetto capture, or real D3D9 frame trace. Host tests and cross-target assembly are not phone FPS, frame-time, GPU-time, or driver-overhead measurements.
- DXVK remains the existing Android-patched v2.5.3. No dependency replacement, SDL migration, feature spoofing, rendering reduction, or frame skipping is in scope.

## Source-led hypotheses and dispositions

### H1 — redundant chunk-table acquire on every forwarded call

`HostTable::Get` currently performs an acquire load of the never-freed chunk pointer, followed by an acquire load of the entry. `Append` constructs/zero-initializes a chunk and then release-stores the entry; `Set` also release-stores entries. A valid published guest index therefore reaches an entry acquire that synchronizes with the entry release and orders the earlier chunk/object initialization. The first acquire may be replaceable with relaxed while retaining the entry acquire.

**Candidate:** change only the chunk-pointer load to `memory_order_relaxed`; leave the entry acquire, release stores, table size/lifetime, guest handles, and every D3D9 call unchanged. Required gates are an exact-source differential test, concurrent publication/TSan stress, ARM64 code-generation evidence, broad regression, and exact-code CI. x86-64 timing is not expected to distinguish acquire from relaxed and cannot establish an ARM64 or phone win by itself. **Pending.**

### H2 — repeated anisotropy setting lookup

Inspection rejected the premise: `MaxAnisotropy()` already uses a function-local static cached value. `SetSamplerState` does not reread settings on every call. No patch. The remaining local-static guard is not being changed without evidence that it matters.

### H3 — shadow-state deduplication in generated setters

No redundant-state cache will be added speculatively. Reset, StateBlock Apply, Get/Set consistency, multithreaded clients, and DXVK-internal state make a bridge-only cache risky without a real call trace. **No patch in this phase unless source/call evidence establishes correctness and relevance.**

## Initial route

1. Extract the exact `HostTable` implementation into baseline/candidate harnesses; test every chunk transition, entry update, and concurrent valid publication.
2. Measure paired host lookup cost as a sanity/regression signal only; inspect x86-64 and ARM64 assembly to identify the actual memory-order change.
3. If correctness and ARM64 code generation support the candidate, make the one-line source change, run broad regressions and exact-code CI, and retain only with explicit limitations.
4. Audit the actual DXVK Android performance patches and lock submission paths for a second source-led lead; do not treat upstream documentation as a measurement.

## HostTable candidate evidence and exact-code CI checkpoint

- Baseline/A-A/candidate normal differentials pass 530,397 checks each with digest `0bdd522368270e83`. Sixteen-round stress passes 2,828,784 checks per variant with digest `57e3812de0a8d383`; four-round TSan stress passes 707,196 checks per variant with digest `35390bd99880b783`. ASan+UBSan (leak detection disabled for intentional process-lifetime chunks) and TSan logs are empty in canonical runs.
- The initial LeakSanitizer exit is retained in `host-table-sanitizer-failures.log`: 917,504 bytes in seven intentional never-freed chunk arrays. It is not counted as a sanitizer pass or a candidate-specific regression.
- Exact production-class ARM64 compilation emits `ldr` for the chunk pointer and `ldar` for the entry; baseline emits two `ldar` operations. x86-64 lookup instructions are identical.
- The first fixed-order x86-64 timing cohort was noise/order confounded (A/A 0.979714, candidate 0.944553) and is not a win. The balanced 72-process cohort has A/A paired median 1.005189 and candidate 1.006302, with 11 candidate wins and 13 losses. No x86-64 performance or phone gain is claimed.
- Candidate commit: `ca25c4603ca3969fbaabd5e2dfc1264a50101925`. Exact-code CI run `41781790100` completed successfully: all six build/test jobs passed and release skipped. The HostTable change is **adopted** as an ARM64 structural/host-correctness candidate, with no measured phone FPS or timing claim.
- Original DLL SHA-256 `d331be205bdde99897952ee88261ab7e63c5c65e36a4660e2a367886a3537071` imports `Direct3DCreate9`. COM calls are vtable-indirect, so dynamic HostTable call frequency is unknown.

## Fixed-config candidates: anisotropy and screenshot guards

### Anisotropy fast path

- Nine explicit Android values match baseline exactly, including clamping below 1 and above 16; Android default 1x and desktop default 16x pass separate baseline/candidate checks. TSan passes with zero bad reads and empty stderr.
- ARM64 baseline `ReadAnisotropy` has an `ldarb` local-static guard and branch/slow-path scaffolding; the internal-global candidate is a direct value load with no guard.
- A same-translation-unit benchmark was rejected because the candidate loop was optimized away (147 ns). Raw output is retained as `aniso-bench-optimized-out*`. The corrected separate-translation-unit, balanced 72-process cohort measures candidate/baseline paired median **0.773922884809737**, 24/24 wins; A/A is **0.9934428515617786**. This is an isolated config-read function, not a D3D9 frame or phone measurement.

### Screenshot fast path with feature preservation

- The candidate moves only fixed `shot` string/pointer initialization to bridge setup. Frame numbering, numbered capture parsing, `shot_now` polling every 16 presents, unlinking, surface readback, and PPM encoding remain unchanged.
- Balanced 72-process default-path timing, including the real unchanged `access("shot_now")` policy, gives candidate/baseline paired median **0.9817026286538004**, 20 wins/4 losses; A/A is **0.9904512717334415**. Baseline ARM64 has two `ldarb` guards; candidate has none.
- Exact-function normal and ASan+UBSan differentials pass **16 cases / 80 checks** across empty, numbered, on-demand, and combined modes. Filenames and PPM bytes match, and `shot_now` is removed as before. This does not test a physical GPU readback.

Independent confirmation with a different order rotation gives anisotropy candidate/baseline **0.7846883221435051**, 36/36 wins (A/A 0.997784); screenshot gives **0.991819521095481**, only 22/36 wins (A/A 0.996348). The screenshot guard move is therefore **rejected as immaterial** for a once-per-Present path despite correct semantics and removed ARM64 guards. The combined external source candidate is not used. The smaller anisotropy-only source candidate is committed as `f5be92170480d4d9a99924f41502108903e0449a`; its exact-code CI run `41784574180` passed all six active jobs and skipped release. Exact production-block fixtures pass 44 processes/176 checks across both platforms, defaults, clamps, and TSan. Independent verification is 70/70 and broad regression is 12/12.

Across the 60 primary+confirmation pairs, anisotropy has median ratio **0.7791195541671706**, 60/60 wins, A/A 0.997651, and median absolute savings of 0.266–0.323 ns per isolated setter. Screenshot has median ratio 0.988630, 42/60 wins, A/A 0.994213; its confirmation absolute saving is only 0.230 ns per Present (primary 1.105 ns). This absolute materiality analysis supports adopting the frequent-setter candidate and rejecting the once-per-Present global-state change. Raw recomputation: `materiality-analysis.json`.

Independent recomputation of the current candidate evidence passes **52/52 checks**. All malformed-output, setup, generator, optimized-out, and sanitizer failures are retained beside the raw evidence.

## DXVK patch audit and non-actions

The existing Android patches already cover incremental state-cache durability/path handling and necessary Mali pipeline-compiler serialization. The patch named `android-performance` is state-cache path/lifetime work, not evidence of a remaining draw-submission optimization. Removing the Mali compile mutex would reintroduce the documented driver race; adding speculative bridge state dedup would risk Reset/StateBlock correctness. No DXVK dependency or patch is changed in Phase 8 at this checkpoint.

### Pinned DXVK source confirmation

The external v2.5.3 checkout is exactly `c707d9026f33b6ab89639f154b6ac5f6326fa037`. `SetStateSamplerState` acquires the device lock, services StateBlock recording, then returns early for an unchanged value; `SetRenderState` likewise has a changed-value check. `D3D9Multithread` uses a recursive spinlock only when the device was created protected, but the game's actual creation flags and redundant-call rates are unknown. The submission queue already has dedicated submit/finish threads, condition variables, and a bounded command-buffer queue. These facts reject a speculative duplicate bridge cache or queue rewrite, not a future measured optimization. Raw audit: `dxvk-source-audit.json`.

### HostTable publication proof

For a valid published index, the writer constructs and zero-initializes the chunk and initializes the host object before the non-null entry's release store. The reader's relaxed atomic chunk load only obtains the never-freed array pointer; the following entry acquire reads that non-null entry and synchronizes with its release, making the earlier chunk and host writes visible. A null entry from an invalid/unpublished index remains outside the valid-call contract as in baseline. `Forget` and object lifetime behavior are unchanged. Source checks and the full happens-before chain are in `memory-order-proof.json`.

## Native ARM64 CI measurement tool

The workflow's `arm64-graphics-tests` job uses `ubuntu-24.04-arm` and executes native ARM64 binaries. The exact-source measurement tool and workflow step were committed as `fdd426729d53db0225194433283b3f57ef23134b` after `f5be921` CI passed. Run `41787828484` compares `c033649` versus current HostTable and `ca25c46` versus current anisotropy, with baseline/A-A/candidate, 20 million calls, 24 balanced rounds, raw JSONL, and an uploaded artifact; its ARM64 outcome remains pending.

The tool's anisotropy path links one identical main object into all variants after an initial different-main harness failed to reproduce the established result. Short/noisy and different-main outputs remain under `ci-tool-*` and are excluded by `ci-tool-development.log`. Final local shared-main results are HostTable candidate/baseline **0.9974109363836161** (16/24, A/A 1.014411; neutral) and anisotropy **0.8155592549654482** (24/24, A/A 1.002361). These are x86-64 tool-validation results, not the pending ARM64 measurements. Workflow YAML parsing passed.

Exact-code CI `41784574180` completed **success** for `f5be92170480d4d9a99924f41502108903e0449a`: driver-package, Bink, Turnip, Android dependencies, native ARM64 graphics, and APK build jobs passed; release remained skipped. Run/job and artifact API records are preserved. The full log redirect failed twice with EOF, explicitly recorded rather than replaced with inferred log text. CI success is build/host-test evidence, not physical-device FPS or pacing proof.

Run `41787828484` completed successfully on `fdd4267`, including its native ARM64 graphics job, and artifact metadata reports the 41,356-byte `arm64-d3d9-bridge` artifact, but artifact download failed at the disallowed redirect host. No timing values are inferred. A checks-based publisher is therefore prepared for the next exact-head run, with `contents: read`/`checks: write` scoped to the ARM64 job. Its exact shell block passed a no-network dry run using a fake `gh`; the neutral check will carry the full result JSON while the raw artifact remains enabled.
