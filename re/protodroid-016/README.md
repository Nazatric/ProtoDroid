# ProtoDroid 0.16 performance program

Status: **Phase 11 texture/Bink/audio/media-path research is active**. Phase 10 is complete after 1h13m48.332251s with zero net production runtime change. Disabled-Present diagnostic gating was correct but rejected/reverted after native ARM64 showed only 102.642780 ns saved per Present; historical audits do not support FIFO/frame-latency/lock or memory/queue rewrites. Present/pacing gates, Android-owned GPU/pipeline/memory audits, and 2,560,000 independently digest-verified allocator masks pass. Validation CI run `41841712692` and APK verification succeeded on `7c4b986`; release was skipped. No physical-device FPS/pacing improvement is claimed.

Fresh Phase 11 Bink profiling passes 23,713 independent checks over 40 processes/11,640 frames. Median 291-frame host totals are 525.858246 ms unregistered and 650.740283 ms registered (paired ratio 1.233614; ~0.431394 ms/frame delta), with exact output and input/plane integrity. Direct copies show required bytes/two live sets dominate and existing tight copies are not faster. Audio ring candidates are rejected: conditional subtract is slower, and span reads save only 0.039957–0.059141 ms CPU/s on native ARM64 despite exact output and 12/12 wins, below the 0.25 ms/s threshold. No H1–H3 production candidate is adopted; H4 texture/media validation remains active.

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
- Phase 6 clean continuation started: `2026-11-04T03:19:00Z`; completion reading: `2026-11-04T04:19:16.064254Z`
- Phase 7 started: `2026-11-04T04:19:50Z`; completion reading: `2026-11-04T05:46:16.333805Z`; continuation recovery noted at `2026-11-04T05:40:16Z`
- Phase 8 started: `2026-11-04T05:47:41Z`; completion reading: `2026-11-04T08:13:01.116479Z`
- Phase 9 started: `2026-11-04T08:13:48Z`; completion reading: `2026-11-04T13:00:05.483947Z`
- Phase 10 started: `2026-11-04T13:03:30.762524Z`; completion reading: `2026-11-04T14:17:19.094775Z`
- Phase 11 started: `2026-11-04T14:18:47.034587Z`; active
- Scope: real, quality-preserving FPS, frame-time, shader-loading, and sustained-performance work only

## Evidence continuity

At `2026-11-04T05:40:16Z`, a fresh environment preserved the working files but not `/home/user/protodroid-016-evidence`. The active branch was verified blob-for-blob against pushed `747db65` and reattached without content changes. Earlier external raw files and manifests are historical summaries, not presently reopenable evidence. Exact commits/CI remain available; regenerated Phase 7 files are clearly separated.

At `2026-11-04T12:49Z`, another fresh shallow clone preserved cumulative worktree bytes but not the external store. The actual active branch was fetched and HEAD/index reattached at validated `39ff0f2c3b8bccc303e2d53e204df8abbc390d81` without changing file contents; only the seven intended research edits remained. Final GitHub run/check metadata and all 360 compact ARM64 rows were reopened and newly verified 390/390. New pipeline/cache audits are labeled as reconstructed evidence; older missing raw files are not claimed as present.

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
| `phase-06.md` | Completion report, synchronization/Present experiments, sustained stress, ADPF attribution, and limitations |
| `phase-07.md` | Phase 7 completion, multi-wait adoption, RCF rejection, and regenerated evidence |
| `phase-08.md` | Phase 8 completion, anisotropy adoption, HostTable/screenshot rejections, and ARM64 evidence |
| `phase-09.md` | Complete shader/cache research, unchanged-backup adoption, SHA-1 materiality rejection, and validation |
| `phase-10.md` | Complete Vulkan GPU/synchronization/memory/presentation audit; rejected diagnostic gating and presentation-policy changes |
| `phase-11.md` | Active texture/Bink/audio/media-path profiling and quality-preserving candidate evaluation |

Large traces, external clones, benchmark binaries, and raw generated results stay outside Git under `/home/user/protodroid-016-evidence`; `/tmp/protodroid-016` is only a compatibility symlink. The original `/tmp` artifacts were lost during an earlier recreation and replaced by explicitly regenerated, hash-verified evidence. At the 2026-11-03T20:27Z recreation, that external store was also absent; Phase 1–3 raw artifacts are therefore historical committed records here, not files claimed to remain locally available. Local checkout copies were preserved under `/home/user/protodroid-016-recovery/2026-11-03T2027Z`, the branch was fast-forwarded to `f1737b23`, and a fresh Phase 4 store records new evidence and its own provenance. No physical-device performance result is inferred from a host benchmark or another project's device.

## Phase status

| Phase | State | Evidence |
| --- | --- | --- |
| 1. Baseline laboratory | Available-environment checkpoint recorded; physical-device gate open | Exact 0.15 host/Bink references, ARM64 CI references, protocol, comparability tests; no runtime optimization delta |
| 2. Five-repository audit | **Complete: available source/host audit; 1h00m01s active work** | All five pins restored/inspected; fresh focused checks cover all five, with 143 hash-verified artifacts and exact-set integrity checks; local patched-DXVK cache baseline passes; Bloodborne UMA/AOT sanitizer negatives and all setup/fixture failures are preserved rather than hidden; ranked ledger/matrix and experiment records populated; no production patch or new ProtoDroid performance run |
| 3. Profiling and bottleneck attribution | **Complete: available source/host profiling; 1h04m22s active work** | DXT/mixer gprof and scaling/destination probes, real RCF and corrected Bink cohorts, instrumentation validation, targeted AArch64 assembly, historical analysis, and a verified 1,281-file manifest; no current device trace or runtime patch |
| 4. Recompiler, dispatch, and ARM64 | **Complete: available source/host research; 1h00m01s** | Exact 126,974-entry standard/locality cohorts, 2,778-process independent verification, and 100 paired-summary checks; provisional split and all alternatives rejected, original runtime restored, permanent collision/concurrent-first-use regression retained, 768 stress processes plus GCC/LLVM and exact-code Linux/ARM64 CI checks passed. No adopted runtime optimization or device claim |
| 5. Native hot paths | **Complete: available host/native research; 1h00m37.516840 active work** | Direct32 + palette-alpha source SHA `40c092c8…`; full differentials, 11,592 dual batch pairs including the size sweep, working-set cohorts, production regressions, real-DLL hot checks, exact-code CI tests, and AArch64 structural checks. No device or packaging-completion claim |
| 6. Scheduling and overlap | **Complete: available host/source research; 1h00m16.064254** | Bounded notification and lazy TID rejected; no runtime optimization adopted. Present regression added; 12/12 regression, sustained normal/TSan stress, concurrent adapter stress, and exact-code CI passed. No device claim |
| 7. Memory, allocation, RCF/I/O | **Complete** | Multi-wait inline storage adopted; RCF deferral rejected; fresh evidence/CI and recovery gates documented |
| 8. D3D9/DXVK overhead | **Complete** | Anisotropy initialization adopted with host/ARM64 evidence; neutral HostTable relaxation reverted; screenshot move rejected; no DXVK replacement |
| 9. Shader/loading/cache behavior | **Complete: 4h46m17.483947s wall-clock** | Byte-validated unchanged-backup optimization adopted with host/ARM64 and exact real-cache evidence; 1,014-entry integrity and specialization variants verified; direct SHA-1 padding and unsafe dedup rejected; real-fixture ARM64 0.073964 independently verified; final full CI/APK succeeded, release skipped |
| 10. Vulkan GPU/sync/memory/presentation | **Complete: 1h13m48.332251s** | H1 rejected/reverted at 0.000102643 ms/Present; H2–H8 no runtime change; 13/13 broad regression, 46 present-path + 25 pacing checks, 4,045 rerun audit checks, 2.56 million allocator masks; validation CI/APK succeeded, release skipped |
| 11. Texture/Bink/audio/media paths | **Active** | Regenerate the exact Bink decode/registered-plane baseline; require output, pitch, synchronization, and both-audio-rate correctness |
| 12–14 | Not started | Sequential execution and evidence gates required |

Current conclusion: Phase 5 retains its quality-preserving host decoder improvement. Phase 6 rejected bounded notification and lazy TID without a runtime scheduling patch. Phase 7 rejected RCF exact-hit basename deferral after 12.96 million exact-output opens showed no paired advantage over A/A; bounded multi-wait storage is adopted in `2e9e31b` after 403.2 million historical actual-handler calls and sanitizer stress; exact-code CI passed. Pre-recreation raw files are unavailable; fresh wait/RCF evidence and exact CI pass. Phases 7, 8, and 9 are complete. Phase 9 adopted exact-byte unchanged-backup skipping, rejected the nanosecond-scale SHA-1 candidate, and disproved exact/naive dedup as a safe optimization. Phase 10 rejected/reverted immaterial Present-diagnostic gating, found no evidence for presentation-policy or memory/queue rewrites, and added focused present-path/frame-pacing plus exhaustive allocator regression coverage without a runtime change. The Android-owned GPU audit and real pipeline/memory censuses found no safe optimization candidate and no MSAA/GPU-time evidence. Phase 10 is complete with fresh regression, reproducibility, exact CI, and APK gates; Phase 11 texture/Bink/audio work is active with a fresh pinned-fixture baseline. PR #38 remains merged at `00976d9`; this continuation creates no merge, tag, or release.
