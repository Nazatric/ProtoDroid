# ProtoDroid 0.16 performance program

Status: **Phase 2 active — five-repository source/provenance audit; substantive checkpoint saved, phase not complete**.

- Released baseline: 0.15, tag `protodroid-v0.15`
- Baseline source: `18ba66bd0945e405aaded0de5ff8848a8cd07932`
- Working branch: `arena/687bae99-protodroid`
- Phase 1 started: `2026-11-03T15:50:07+00:00`
- Phase 2 started: `2026-11-03T15:58:37+00:00`
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
| `phase-02.md` | Active Phase 2 source-audit checkpoint; not a completion report |

Large traces, external clones, benchmark binaries, and raw generated results stay outside Git under `/tmp/protodroid-016`. Their provenance and hashes are referenced from these records. No physical-device performance result is inferred from a host benchmark or another project's device.

## Phase status

| Phase | State | Evidence |
| --- | --- | --- |
| 1. Baseline laboratory | Available-environment checkpoint recorded; physical-device gate open | Exact 0.15 host/Bink references, ARM64 CI references, protocol, comparability tests; no runtime optimization delta |
| 2. Five-repository audit | **Active; below the one-hour completion gate** | All five pins and implementations inspected; focused GoW III/ISS/Bloodborne host correctness tests passed; ledger/matrix populated; no production patch or new ProtoDroid performance run |
| 3–14 | Not started | Sequential execution and evidence gates required |

Current source-audit conclusion: several attractive external techniques are already present, belong to incompatible architectures, or have neutral/negative evidence in their own documents. The next useful work is profiling existing ProtoDroid bottlenecks, not forcing a patch from every repository.
