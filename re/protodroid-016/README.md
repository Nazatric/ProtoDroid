# ProtoDroid 0.16 performance program

Status: **Phase 6 scheduling/frame-pacing research is active** from merged Phase 1–5 commit `00976d9`; Phase 5 completed in 1h00m37.516840 and direct32 + palette-alpha is adopted. No physical-device FPS, frame-pacing, sustained-performance, or 0.16 release improvement is claimed.

- Released baseline: 0.15, tag `protodroid-v0.15`
- Baseline source: `18ba66bd0945e405aaded0de5ff8848a8cd07932`
- Working branch: `arena/5eb1c06c-protodroid`
- Phase 1 started: `2026-11-03T15:50:07+00:00`
- Phase 2 started: `2026-11-03T15:58:37+00:00`
- Phase 3 started: `2026-11-03T18:21:53+00:00`
- Phase 3 completion reading: `2026-11-03T19:26:15+00:00`
- Phase 4 started: `2026-11-03T20:28:45+00:00`
- Phase 4 completion reading: `2026-11-03T21:28:46Z`
- Phase 5 started: `2026-11-03T21:29:04Z`
- Phase 5 completion reading: `2026-11-03T22:29:41.516840Z`
- Phase 6 clean continuation started: `2026-11-04T03:19:00Z`
- Scope: real, quality-preserving FPS, frame-time, shader-loading, and sustained-performance work only

## Research index

| File | Purpose |
| --- | --- |
| `baseline.md` | Reproducible baseline, environment, scenes, settings, and measurement protocol |
| `repository-ledger.md` | Pinned external-source revisions, provenance, licenses, and implementation references |
| `technique-matrix.md` | Ranked candidate techniques and adoption state |
| `profiling.md` | Measured ProtoDroid bottlenecks and profiler evidence |
| `experiment-ledger.md` | Hypotheses, exact revisions, raw results, and adopt/reject decisions |
| `device-results.md` | Physical-device results and explicit coverage gaps |
| `rejected-changes.md` | Negative results and unsafe/misleading non-actions |
| `final-report.md` | Final evidence summary; not a release claim while work is active |
| `phase-01.md` | Phase 1 available-environment checkpoint; physical baseline remains open |
| `phase-02.md` | Phase 2 completion report, raw host/source results, and limitations |
| `phase-03.md` | Phase 3 completion report, host/assembly profiles, bottleneck map, and limitations |
| `phase-04.md` | Completion report, dispatch/layout measurements, ARM64 structure, and final regression evidence |
| `phase-05.md` | Completion report, controlled DXT A/B results, adopted direct32/palette paths, rejected alternatives, CI, and limitations |
| `phase-06.md` | Active synchronization, scheduling, and frame-pacing audit |

Large traces, external clones, benchmark binaries, and raw generated results stay outside Git under `/home/user/protodroid-016-evidence`; `/tmp/protodroid-016` is only a compatibility symlink. The original `/tmp` artifacts were lost during an earlier recreation and replaced by explicitly regenerated, hash-verified evidence. At the 2026-11-03T20:27Z recreation, that external store was also absent; Phase 1–3 raw artifacts are therefore historical committed records here, not files claimed to remain locally available. Local checkout copies were preserved under `/home/user/protodroid-016-recovery/2026-11-03T2027Z`, the branch was fast-forwarded to `f1737b23`, and a fresh Phase 4 store records new evidence and its own provenance. No physical-device performance result is inferred from a host benchmark or another project's device.

## Phase status

| Phase | State | Evidence |
| --- | --- | --- |
| 1. Baseline laboratory | Available-environment checkpoint recorded; physical-device gate open | Exact 0.15 host/Bink references, ARM64 CI references, protocol, comparability tests; no runtime optimization delta |
| 2. Five-repository audit | **Complete: available source/host audit; 1h00m01s active work** | All five pins restored/inspected; fresh focused checks cover all five, with 143 hash-verified artifacts and exact-set integrity checks; local patched-DXVK cache baseline passes; Bloodborne UMA/AOT sanitizer negatives and all setup/fixture failures are preserved rather than hidden; ranked ledger/matrix and experiment records populated; no production patch or new ProtoDroid performance run |
| 3. Profiling and bottleneck attribution | **Complete: available source/host profiling; 1h04m22s active work** | DXT/mixer gprof and scaling/destination probes, real RCF and corrected Bink cohorts, instrumentation validation, targeted AArch64 assembly, historical analysis, and a verified 1,281-file manifest; no current device trace or runtime patch |
| 4. Recompiler, dispatch, and ARM64 | **Complete: available source/host research; 1h00m01s** | Exact 126,974-entry standard/locality cohorts, 2,778-process independent verification, and 100 paired-summary checks; provisional split and all alternatives rejected, original runtime restored, permanent collision/concurrent-first-use regression retained, 768 stress processes plus GCC/LLVM and exact-code Linux/ARM64 CI checks passed. No adopted runtime optimization or device claim |
| 5. Native hot paths | **Complete: available host/native research; 1h00m37.516840 active work** | Direct32 + palette-alpha source SHA `40c092c8…`; full differentials, 11,592 dual batch pairs including the size sweep, working-set cohorts, production regressions, real-DLL hot checks, exact-code CI tests, and AArch64 structural checks. No device or packaging-completion claim |
| 6. Scheduling and overlap | **Active** | Exact wait/notify audit and preserved baseline; no candidate result yet |
| 7–14 | Not started | Sequential execution and evidence gates required |

Current conclusion: Phase 4 was an honest negative dispatch-optimization result. Phase 5 has a positive host decoder result: direct DXT2–5 BGRA rows and invariant palette-alpha removal reduce measured host decode time while preserving every tested output. Direct16, rowtile, noinline, and lazy-alpha forms are rejected. These results remain synthetic host evidence, not Android FPS, frame pacing, or sustained-device proof. Phase 6 is auditing scheduling/wait behavior with exact model fixtures before considering any policy change. PR #38 merged the Phase 1–5 work at `00976d9`; this continuation creates no merge, tag, or release.
