# ProtoDroid 0.16 performance program

Status: **Phase 3 complete for available source/host profiling (1h04m22s); Phase 4 recompiler/dispatch work is active**. The provisional split lookup was rejected after independent localized regressions and reverted; no runtime optimization or physical-device improvement is currently claimed.

- Released baseline: 0.15, tag `protodroid-v0.15`
- Baseline source: `18ba66bd0945e405aaded0de5ff8848a8cd07932`
- Working branch: `arena/687bae99-protodroid`
- Phase 1 started: `2026-11-03T15:50:07+00:00`
- Phase 2 started: `2026-11-03T15:58:37+00:00`
- Phase 3 started: `2026-11-03T18:21:53+00:00`
- Phase 3 completion reading: `2026-11-03T19:26:15+00:00`
- Phase 4 started: `2026-11-03T20:28:45+00:00`
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
| `phase-04.md` | Active recompiler/dispatch/ARM64 measurement record |

Large traces, external clones, benchmark binaries, and raw generated results stay outside Git under `/home/user/protodroid-016-evidence`; `/tmp/protodroid-016` is only a compatibility symlink. The original `/tmp` artifacts were lost during an earlier recreation and replaced by explicitly regenerated, hash-verified evidence. At the 2026-11-03T20:27Z recreation, that external store was also absent; Phase 1–3 raw artifacts are therefore historical committed records here, not files claimed to remain locally available. Local checkout copies were preserved under `/home/user/protodroid-016-recovery/2026-11-03T2027Z`, the branch was fast-forwarded to `f1737b23`, and a fresh Phase 4 store records new evidence and its own provenance. No physical-device performance result is inferred from a host benchmark or another project's device.

## Phase status

| Phase | State | Evidence |
| --- | --- | --- |
| 1. Baseline laboratory | Available-environment checkpoint recorded; physical-device gate open | Exact 0.15 host/Bink references, ARM64 CI references, protocol, comparability tests; no runtime optimization delta |
| 2. Five-repository audit | **Complete: available source/host audit; 1h00m01s active work** | All five pins restored/inspected; fresh focused checks cover all five, with 143 hash-verified artifacts and exact-set integrity checks; local patched-DXVK cache baseline passes; Bloodborne UMA/AOT sanitizer negatives and all setup/fixture failures are preserved rather than hidden; ranked ledger/matrix and experiment records populated; no production patch or new ProtoDroid performance run |
| 3. Profiling and bottleneck attribution | **Complete: available source/host profiling; 1h04m22s active work** | DXT/mixer gprof and scaling/destination probes, real RCF and corrected Bink cohorts, instrumentation validation, targeted AArch64 assembly, historical analysis, and a verified 1,281-file manifest; no current device trace or runtime patch |
| 4. Recompiler, dispatch, and ARM64 | **Active** | Exact 126,974-entry standard/locality cohorts and 2,778-process independent verification; provisional split rejected/reverted after localized regressions, alternatives exhausted, permanent collision regression retained, generated source/AArch64 structure audited. No phase-completion or device claim |
| 5–14 | Not started | Sequential execution and evidence gates required |

Current conclusion: several attractive external techniques are already present, incompatible, or unsupported by their own evidence. Phase 3 supplies controlled host costs and structural dispatch/TSO candidates but no common device frame denominator. Phase 4 has independently verified standard and localized host evidence, but every dispatch-layout candidate has material tradeoffs; the provisional split is reverted and no TSO semantics changed. Synthetic nanoseconds, RSS, and instruction counts must not become Android FPS or pacing claims.
