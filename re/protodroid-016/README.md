# ProtoDroid 0.16 performance program

Status: **Phase 14 final evidence/release-readiness audit is complete within available source/host/native-CI scopes; not release-ready**. Phase 12 ran 3h04m49.810437s and adopted one source-provable XAudio2/FAudio master-lifetime correctness fix in `c8709cb5a553b470f8724dce15faae0779a85528`; it did not adopt a performance optimization or claim a device issue fix. Fresh evidence includes 61/61 broad-regression commands across 11 groups, 605 independent raw-evidence checks, 43-check real-backend controls in all four modes, and 32-cycle normal-lifecycle runs (502/470 checks). Exact CI run `41900484812` passes every required job on `c8709cb`; release was skipped. No physical-device FPS/pacing, media, rendering, thermal, or sustained-play improvement is claimed.

The five investigated reports—**#22, #29, #32, #33, and #35—remain open**, not fixed. In #35, a same-revision/settings previous run reached frame 65,536 and opened mission Bink videos, while the failing run stopped before mission Bink and later showed watchdog/`LockRect`/device-loss correlations. This does not justify a speculative Bink, driver, or lock patch. Package evidence is exact-revision CI verification plus current Phase 13 GitHub artifact ZIP digest `0a980739…`; signed-redirect EOF prevented local inner-APK/library hashing or installation claims.

**Post-Phase-14 follow-up:** `issue-32-maintenance4.md` records production commit `e1277dbc6448c27c10d1ee58070b1eefeb10da85`, a source-provable Android compatibility correction: optional maintenance4 now follows physical support on Vulkan 1.3 as well as 1.2, using DXVK's existing temporary-resource fallback. The focused differentiating test and fresh 62/62 broad host regression pass, with 442 independent manifest/raw-output checks. Exact `android-deps` CI `41927692571` and `build-apk` CI `41927692628` are green; 52 metadata checks pass and the release job remains skipped. No affected-device run, FPS result, #32 resolution, or release-readiness change is claimed.

**Post-Phase-14 Vulkan recovery follow-up:** `issue-33-wrapper-recovery.md` records production commits `e3122267ca8f41ef845abfd58eaeb45c28ae0e3f` and `99db3e8b2d9fefab8fe62526b086f6ba834fe5a1`. Explicit driver actions now clear both Turnip and wrapper same-build failure guards in native's actual external app-specific directory; ordinary Auto launches retain crash-loop protection and Custom policy is unchanged. The final 62/62 broad regression and 442 raw-output checks pass, alongside 26 mocked production-probe checks. Exact final `build-apk` CI `41936405825` is green with 53 independent metadata checks; release remains skipped. #33 remains open with no affected-device, FPS, or release-readiness claim.

**Candidate package identity:** at the user's explicit request on 2026-11-05, the open PR manifest advances from 0.15/code 15 to **0.16/code 16** through `tools/bump_version.py`. This corrects the candidate's visible package version and Android update identity; it does not alter the released 0.15 artifact, authorize a merge/tag/release, or satisfy the open physical-device gates.

**Post-Phase-14 Skate 3/performance investigation:** `skate3-performance-investigation.md` analyzes the newly supplied `61869cbb…` diagnostic-tour capture and the real Skate 3/RexGlue sources. It separates cold pipeline/Present/LockRect stalls from unclassified ordinary-play cost and from Skate 3's native-renderer and quality-profile advantages. No production runtime optimization, device FPS gain, or issue fix is adopted or claimed; the next gate is a matched warm ordinary-play baseline plus targeted DXVK/pipeline/lock attribution.

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
- Phase 11 started: `2026-11-04T14:18:47.034587Z`; completion reading: `2026-11-04T17:15:07.338209Z`
- Phase 12 started: `2026-11-04T17:17:24.420617Z`; completion reading: `2026-11-04T20:22:14.231054Z`
- Phase 13 started: `2026-11-04T20:32:10.615107Z`; completion reading: `2026-11-04T21:32:21.772245Z`
- Phase 14 started: `2026-11-04T21:33:17.494345Z`; completion reading: `2026-11-04T22:33:22.182059Z`
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
| `spartanport-shader-warmup.md` | User-requested SpartanPort warm-cache/startup audit, DXVK baseline equivalence, focused helper tests, and feasibility boundary |
| `skate3-performance-investigation.md` | Fresh supplied-log census, real Skate 3/RexGlue comparison, cold/steady attribution boundaries, and ranked next experiments |
| `phase-10.md` | Complete Vulkan GPU/synchronization/memory/presentation audit; rejected diagnostic gating and presentation-policy changes |
| `phase-11.md` | Complete texture/Bink/audio/media profiling, sustained validation, issue audit, and candidate dispositions |
| `phase-12.md` | Complete sustained/memory/lifecycle research, XAudio2 master-lifetime adoption, real-backend controls, and exact CI evidence |
| `phase-13.md` | Integrated Bink/FAudio controls, transient audio-open recovery adoption, broad regression, exact CI/package evidence, and limitations |
| `phase-14.md` | Final net-production, evidence, version/package, release-boundary, and unmeasured-device audit |
| `issue-32-maintenance4.md` | Post-Phase-14 capability-driven maintenance4 correction, differentiating regression, CI/device boundaries, and open-#32 disposition |
| `issue-33-wrapper-recovery.md` | Post-Phase-14 explicit Vulkan recovery and native external-marker-directory correction, negotiation audit, regression evidence, and open-#33 disposition |

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
| 11. Texture/Bink/audio/media paths | **Complete: 2h56m20.303622s; zero net production change** | 1,088,025-check aggregate gate; 40-process Bink baseline, 96-process copies, x86/ARM64 audio rejections, 100-loop media stress, 53-command regression, 72-check issue audit, required CI/APK steps passed, release skipped |
| 12. Sustained whole-program/memory/frame-stall research | **Complete: 3h04m49.810437s** | XAudio2/FAudio master-lifetime fix adopted in `c8709cb`; 61/61 broad commands, 605 independent checks, 43×4 real-backend controls, 32-cycle normal lifecycle, exact CI `41900484812` success; no FPS/device issue claim |
| 13. Integrated correctness/performance validation | **Complete: 1h00m11.157138s** | Bink transient-output retry adopted in `2dabecd`; 62/62 broad commands, 94 concurrent checks, 40/49 integrated controls, exact CI `41911558127` success; no FPS/device issue claim |
| 14. Final evidence/release-readiness audit | **Complete: 1h00m04.687714s; source/host/native-CI scope only** | Two fresh 62/62-command regressions with 316 and 378 independent checks; 1,464 document/raw, 1,099 quantitative/scope, and 371 ARM64 checks; six net native changes inventoried; fresh Bink and XAudio2 rebuilds pass; v0.15/latest release preserved, five issues open; no merge/tag/release |

Current conclusion: Phase 5 retains its quality-preserving host decoder improvement. Phase 6 rejected bounded notification and lazy TID without a runtime scheduling patch. Phase 7 rejected RCF exact-hit basename deferral after 12.96 million exact-output opens showed no paired advantage over A/A; bounded multi-wait storage is adopted in `2e9e31b` after 403.2 million historical actual-handler calls and sanitizer stress; exact-code CI passed. Pre-recreation raw files are unavailable; fresh wait/RCF evidence and exact CI pass. Phases 7, 8, and 9 are complete. Phase 9 adopted exact-byte unchanged-backup skipping, rejected the nanosecond-scale SHA-1 candidate, and disproved exact/naive dedup as a safe optimization. Phase 10 rejected/reverted immaterial Present-diagnostic gating, found no evidence for presentation-policy or memory/queue rewrites, and added focused present-path/frame-pacing plus exhaustive allocator regression coverage without a runtime change. The Android-owned GPU audit and real pipeline/memory censuses found no safe optimization candidate and no MSAA/GPU-time evidence. Phase 10 completed with fresh regression, reproducibility, exact CI, and APK gates. Phase 11 completed with a fresh pinned-fixture baseline, sustained media and audio evidence, no adopted runtime candidate, and all five supplied issues still open. Phase 12 completed with a source-provable XAudio2/FAudio master-lifetime fix. Phase 13 adds a source-provable Bink failed-output retry, preserves healthy-stream ownership, and passes integrated, concurrent, broad, and exact CI controls; no performance or affected-device fix is claimed. Phase 14 has consolidated the final evidence; the physical-device and release gates remain open. PR #38 remains merged at `00976d9`; this continuation creates no merge, tag, or release.
