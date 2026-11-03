# ProtoDroid 0.16 performance program

Status: **Phase 1 active — reproducible baseline and measurement laboratory**.

- Released baseline: 0.15, tag `protodroid-v0.15`
- Baseline source: `18ba66bd0945e405aaded0de5ff8848a8cd07932`
- Working branch: `arena/687bae99-protodroid`
- Phase 1 started: `2026-11-03T15:50:07+00:00`
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

Large traces, external clones, benchmark binaries, and raw generated results stay outside Git under `/tmp/protodroid-016`. Their provenance and hashes are referenced from these records. No physical-device performance result is inferred from a host benchmark.

## Phase status

| Phase | State | Evidence |
| --- | --- | --- |
| 1. Baseline laboratory | Active | Environment verified; protocol and host baseline being established |
| 2. Five-repository audit | Not started | Begins only after the Phase 1 exit gate |
| 3–14 | Not started | Sequential execution required |
