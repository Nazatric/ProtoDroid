# ProtoDroid 0.16 performance program

Status: **Phase 2 complete — five-repository source/provenance audit; Phase 3 profiling is next**.

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
| `phase-02.md` | Phase 2 completion report, raw host/source results, and limitations |

Large traces, external clones, benchmark binaries, and raw generated results stay outside Git under persistent `/home/user/protodroid-016-evidence`; `/tmp/protodroid-016` is only a compatibility symlink. The original `/tmp` artifacts were lost during sandbox recreation. Current provenance/manifests/logs are explicitly regenerated and hash-verified, not represented as recovered originals. No physical-device performance result is inferred from a host benchmark or another project's device.

## Phase status

| Phase | State | Evidence |
| --- | --- | --- |
| 1. Baseline laboratory | Available-environment checkpoint recorded; physical-device gate open | Exact 0.15 host/Bink references, ARM64 CI references, protocol, comparability tests; no runtime optimization delta |
| 2. Five-repository audit | **Complete: available source/host audit; 1h00m01s active work** | All five pins restored/inspected; fresh focused checks cover all five, with 143 hash-verified artifacts and exact-set integrity checks; local patched-DXVK cache baseline passes; Bloodborne UMA/AOT sanitizer negatives and all setup/fixture failures are preserved rather than hidden; ranked ledger/matrix and experiment records populated; no production patch or new ProtoDroid performance run |
| 3–14 | Not started | Sequential execution and evidence gates required |

Current source-audit conclusion: several attractive external techniques are already present, belong to incompatible architectures, or have neutral/negative evidence in their own documents. The next useful work is profiling existing ProtoDroid bottlenecks, not forcing a patch from every repository.
