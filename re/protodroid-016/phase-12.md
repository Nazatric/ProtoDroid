# Phase 12 — Sustained whole-program, memory, frame-stall, and lifecycle research

Status: **complete**. Started **2026-11-04T17:17:24.420617Z** from Phase 11 completion `8748eafe8abf8ca6cc1a01515e42db4e7789eb44`; completion reading **2026-11-04T20:22:14.231054Z** (**11,089.810437 seconds**, 3h04m49.810437s). Production/CI validation head is `c8709cb5a553b470f8724dce15faae0779a85528`. Phase 13 is next. Issues **#22, #29, #32, #33, and #35 remain open**; this phase does not claim an affected-device fix or a physical-device performance result.

## Exact source, fixtures, and settings

- Phase 11 validation source was `b5c9e50043bd114520b9acc9dfee8fe65016aabe`; the Phase 12 production change is the single reviewed XAudio2 lifetime fix in `c8709cb5a553b470f8724dce15faae0779a85528`. The candidate/source SHA-256 is `5b1bc983ef1fa0788af1c2d2e2494acb5383afdc80ec9f2512b69e9451e329ad`; the preserved unpatched control source is `b09dbcdb17b93865778ef3e9179e35d6f1bc021d969c22fc4417a88129359a2d`.
- The exact production `src/native/xaudio2.cpp` was compiled with the pinned FAudio 25.10 header and, for real-backend controls, freshly built static FAudio `ba876ce3be73eabd7094fa276a751ede8328b608` and SDL `c98c4fbff6d8f3016a3ce6685bf8f43433c3efcc`. The real backend uses host dummy audio. It is not Android/Bionic execution, an audible-output test, or a physical-device timing test.
- The four permanent fake-backend modes are `audio_master_reuse=0/1` × `audio_quantum_1024=0/1`. The production defaults and both quantum behaviors are unchanged. ASan/UBSan runs use `detect_leaks=0` only because the production bridge intentionally retains guest vtables/blocks and other process-lifetime allocations; address and undefined-behavior checks remain enabled.
- Fresh Bink regression uses the verified external `valve.bik` fixture (15,563,652 bytes; SHA-256 `e4cc59c1242615fc33159537e21d019599481676f1de62818a5cc4aa9d2f2dd5`), pinned zlib `51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf`, and FFmpeg `db69d06eeeab4f46da15030a80d539efb4503ca8`. Patched DXVK source is v2.5.3 `c707d9026f33b6ab89639f154b6ac5f6326fa037` with the existing ordered Android patches. These are correctness/regression controls, not new performance optimizations.
- Historical logs retain their original source revisions, drivers, settings, and instrumentation. The long-run raw inputs discussed in earlier summaries are not currently available; the fresh available-log audit explicitly covers only the two short historical logs. No unavailable raw result is represented as newly reproduced.
- No ADB, affected physical device, Android/Bionic execution, real audio device, GPU profiler, thermal trace, or current device frame-time capture is available. Host/native-CI evidence is kept separate from phone FPS, frame pacing, rendering, audio quality, and sustained thermal evidence.

## Non-negotiable invariants

Real engine frames and frame pacing, dynamic shadows, Bink/cutscenes, A/V synchronization, both audio-rate behaviors, safe fallbacks, mods, saves, Classic controls, FIFO/Reset safety, capability-driven Vulkan, and prior runtime dispositions are preserved. No generated-frame metric, frame skipping, hidden quality/resolution reduction, unconditional media skip, feature spoofing, stale-pointer aliasing, or speculative lock/lifetime rewrite is allowed. The adopted change is a correctness/lifetime repair, not a license to claim an FPS or issue-resolution improvement.

## Source-led hypotheses and outcomes

### H1 — sustained historical memory/frame-stall attribution

The historical sustained summaries were reviewed separately from the short diagnostic tours. They suggest substantial early/whole-run memory growth and late virtual-memory movement, but whole-process RSS/virtual values do not isolate guest arenas, DXVK/GPU allocations, driver caches, or leaks. Interval/EngineService correlations are much stronger than interval/Present-wall correlations in the summarized data, but correlation does not identify a causal stall or justify allocator, queue, presenter, or quality changes.

The fresh `available-log-audit.json` passes **21 checks** over `proto-native.log` and `proto-native.prev.log`: durations **61.544/67.945 s**, **3,131/3,007** Present intervals, and worst intervals **1,032.764/3,430.826 ms**. It marks `long_run_raw_available=false`. The older long-run figures remain summary-only and are not a current 0.16 A/B.

**Disposition:** attribution only; no allocator, memory-policy, frame-limiter, queue, presentation, or quality change. A current, comparably instrumented device soak remains necessary before a sustained FPS/thermal/memory claim.

### H2 — actual XAudio2/FAudio lifecycle and retained-output behavior

The source audit found a concrete lifetime defect in the old non-retained path and in retained-master configuration replacement: XAudio2 2.3 exposes `DestroyVoice` as void, while FAudio can refuse destruction when another voice still sends to the target. The old code discarded the master pointer regardless of that refusal and could then overwrite FAudio's live master pointer on the next create. This is a reproducible host lifecycle defect, not proof that any particular phone report has the same trigger.

The recovered game mixer-thread disassembly uses source-voice operations and contains no direct mastering create/destroy calls. The review also verified that pinned FAudio 25.10 declares, defines, and exports `FAudioVoice_DestroyVoiceSafeEXT`, and that Android `build-deps.sh` uses that tag. Indirect or unseen callback reentry and physical-device behavior remain unproven.

**Disposition:** the defect justified the smallest preserving candidate and focused negative controls; no audio mixer, callback, quantum, or media rewrite was selected.

### H3 — sustained lifecycle candidate only if a source-provable defect exists

The adopted change tracks every real master (`live_master`), uses `FAudioVoice_DestroyVoiceSafeEXT` where a real master is destroyed, preserves the old pointer on refusal, and retries at a later create only when no logical mastering wrappers remain. Mastering operations are serialized in both reuse modes. Logical wrapper tombstones and engine-release orphan cleanup retain their existing fail-closed behavior. The change does not skip source destruction, detach sends implicitly, alter audio rates, or suppress errors into a claimed success.

The permanent regression was added to `src/native/tests/xaudio2_test.cpp` and `run_xaudio2_test.sh`, and CI checks out the exact FAudio commit before running all four modes. Real-backend fixtures additionally exercise the actual pinned FAudio/SDL implementation rather than only the fake model.

**Disposition:** **adopted in `c8709cb` as a lifecycle correctness fix**. No performance delta is inferred. Further callback-reentry or device-specific changes require new evidence.

### H4 — broad sustained regression and unresolved correctness reports

The fresh broad runner completed **61/61 commands across 11 groups in 123.423438 s**, including launcher/policy, diagnostics/settings, DXVK policy, mods, native core, fake and real XAudio2, Bink at 32/44.1 kHz, real-DLL differential, and reproducibility gates. An independent raw-evidence verifier passed **605 checks**. The available-log audit passed **21 checks** and did not recreate unavailable long-run inputs.

The supplied issue evidence remains diagnostic only. #35 still has a working same-revision/settings control alongside a failing watchdog/`LockRect`/device-loss run; #33's latest recurrence lacks current logs; #32 lacks the exact unsupported feature; #22 lacks a visual root cause; #29 lacks actionable logs. None is closed or relabeled as fixed by the lifecycle repair.

## Phase 12 completion report

### 1. Actual time and work

Phase 12 ran from **2026-11-04T17:17:24.420617Z** to **2026-11-04T20:22:14.231054Z**, **11,089.810437 seconds (3h04m49.810437s)** of substantive source/dependency audit, original-driver disassembly, historical-log classification, fake and real FAudio fixture construction, baseline/candidate negative controls, sanitizer and concurrency testing, normal-lifecycle stress, Bink/DXVK/real-DLL regression, independent evidence verification, CI/package inspection, and documentation. It began at Phase 11 completion `8748eaf`; the exact production and CI head is `c8709cb5a553b470f8724dce15faae0779a85528`. No timer padding or physical-device claim is included.

### 2. Files and commits

Production commit **`c8709cb5a553b470f8724dce15faae0779a85528` (`fix: preserve FAudio master during voice replacement`)** changed only:

- `src/native/xaudio2.cpp`: live-master bookkeeping, SafeEXT destruction/refusal handling, and synchronized mastering lifecycle.
- `src/native/tests/xaudio2_test.cpp`: permanent exact-production four-mode regression with dependent-send negative controls.
- `src/native/tests/run_xaudio2_test.sh`: warning-clean ASan/UBSan four-mode runner.
- `.github/workflows/build-apk.yml`: pinned FAudio checkout and regression execution in `bink-tests`.

This completion record updates only research documentation: `phase-12.md`, `README.md`, `experiment-ledger.md`, `profiling.md`, `technique-matrix.md`, `rejected-changes.md`, `repository-ledger.md`, `device-results.md`, and `final-report.md`. External raw logs, pinned checkouts, binaries, disassembly, runners, CI JSON, and verifier outputs remain under `/home/user/protodroid-016-evidence/phase-12/regenerated/`; no large artifact is added to Git. No merge, tag, or release was created.

### 3. Candidate dispositions

- **Adopted:** the narrow XAudio2/FAudio master-lifetime repair in `c8709cb`. It prevents pointer loss/overwrite after a refused real-master destruction and preserves the existing retained-output and non-retained semantics.
- **Rejected as unjustified:** allocator/memory-policy rewrites, Present/FIFO/queue changes, mixer/DSP changes, callback or quantum changes, implicit sender detachment, and any device-specific workaround derived only from correlation. Earlier rejected optimizations remain closed absent new workload evidence.
- **Not selected:** a speculative fix for #22, #29, #32, #33, or #35. The source-provable host defect is not promoted to an affected-device diagnosis.
- **Preserved:** DXT work, Bink/cutscenes, dynamic shadows, AA/texture quality, mods/metadata, texture modding, A/V synchronization, both audio-rate behaviors, ARM64 ABI, safe defaults/drivers, native launcher/controls, and all prior runtime dispositions.

### 4. Raw before/after measurements

This phase's adopted change is a correctness repair, so there is **no honest FPS before/after number**. The freshly reopened raw lifecycle controls are:

| Fresh control | Before/unpatched | After/current production |
| --- | --- | --- |
| Permanent fake-backend regression | Reuse negative control fails at check 23; old pointer can be overwritten | **47 checks passed in each of four modes**, zero master overwrites; reuse master create/destroy **2/1**, non-reuse **2/2**; refusal counts **1/2** respectively |
| Real pinned FAudio/SDL bridge, four modes | All four builds succeed; all four runs return 1 at **check 23**, “dependent-send replacement returns refusal” | **43 checks passed in each of four modes**; `old_master_preserved=1`, `detach_retry=1`, empty sanitizer stderr |
| Normal real-backend lifecycle | No normal-lifecycle result was counted from the first fixture compile; it stopped on an unused fixture helper | **32 source/master recreation cycles + orphan cleanup**; **502 checks reuse=1**, **470 checks reuse=0**, both sanitizer-clean |

The following are **preserved historical Phase 12 control summaries**, not newly regenerated raw files: graph baseline **35 checks/one overwrite** versus candidate **37/zero**; detach baseline **35/35 with one overwrite** versus candidate **38/38 with zero**; threaded baseline reuse **7,694 checks passed** and non-reuse failure at **check 7,689**, versus candidate reuse **7,694 checks/1,536 successes** and non-reuse **4,544 checks/486 successes** with 1,050 invalid concurrent-second-master results and zero overwrites. The expanded safe-API matrix summary is **502/486/502/486 checks**, and blocking fixtures are **36 checks per mode**. These historical controls informed the adopted patch but are not presented as fresh final-gate inputs.

Historical sustained summaries, likewise not newly available raw files, report run A **902.58 s/181 windows/53 pipelines/41,954 intervals** and run B **521.98 s/105/11/37,105**. RSS/virtual growth was A **+1,225/+5,369 MiB**, B **+1,045/+5,557 MiB**; last-20-window deltas were A **+2/+50**, B **+72/+27 MiB**. Interval/EngineService correlations were **0.961678/0.643686**, versus interval/Present-wall **0.002148/−0.204270**. These figures do not establish leaks, causation, or a candidate improvement.

### 5. Correctness and regression evidence

- Fresh broad regression: **61/61 commands, 11 groups, 123.423438 s**, with independent **605-check** raw verification. The real cache remains **1,014/1,014 valid entries**; the real-DLL differential retains its zero-difference markers. The independent phase finalizer passes **407/407 checks**; its first invocation failed only its own porcelain-status whitespace parser and is preserved separately.
- Fresh available-log audit: **21 checks**, explicitly not a long-run reproduction.
- Real-backend normal lifecycle: **502 + 470 checks**, 32 cycles per mode plus orphan cleanup, empty ASan/UBSan stderr.
- Real-backend production controls: **43 checks × 4 modes**, empty sanitizer stderr; unpatched negative controls all fail at the expected check 23 and are not counted as passes.
- Expanded safe-API matrix: **502/486/502/486 checks**; graph/detach and threaded controls retain their raw baseline failures and candidate zero-overwrite results. Blocking fixtures completed **36 checks per mode**.
- CI run **41900484812** on exact head `c8709cb5a553b470f8724dce15faae0779a85528` is **completed/success**. Required dependency, native, ARM64, driver, Turnip, and APK jobs succeed; optional ARM64 evidence jobs are neutral; `release` is skipped. The new pinned-FAudio checkout and four-mode XAudio2 regression pass in `bink-tests` steps 8–9.
- `build-apk` source build step 18, APK packaging/signing step 26, and APK/embedded-DXVK verification step 28 pass. The prebuilt-symbol fallback step 22 is skipped. Artifact **13359379211** (`prototype-apk`) is reported by GitHub as **57,955,156 bytes**; this is artifact metadata, not an inner-APK hash. The signed blob redirect returned EOF, so no local APK/library digest or installation claim is made.

### 6. Limitations

No ADB, affected physical device, Android/Bionic execution, real audio device, GPU profiler, thermal trace, or local Android NDK is available. Host fake/real-backend tests do not establish audible quality, callback latency on a phone, Android synchronization, FPS, frame-time percentiles, GPU time, thermal throttling, or sustained playback. Native ARM64 CI is not a physical Mali/Adreno device.

The long-run raw artifacts are unavailable; only the two short historical logs were freshly audited. Historical RSS/virtual growth and correlations are not leak or causation proof. The recovered mixer-thread disassembly has no direct mastering lifecycle calls, but indirect/unseen callback reentry is not proven impossible. The fixture's first unused-helper compile failure, the broad runner's earlier setup failure, the verifier's initial stdout-only lookup error, the finalizer's initial porcelain-whitespace parser failure, and the APK CLI/setup error followed by signed-blob EOF are preserved as setup/evidence limitations rather than hidden or counted as product passes.

The five supplied issues remain **open and unfixed by this program**. In particular, the lifecycle defect's host reproduction does not prove that Dana's Safe House/cutscene black-screen/audio report, the Motorola Play-to-home crash, the Itel setup report, or the Mali visual glitch shares this trigger. No feature spoofing, unconditional cutscene skip, quality reduction, or universal driver workaround is justified.

### 7. Next priority

Begin **Phase 13** with integrated correctness/performance validation planning and the remaining source-led whole-program work, keeping the adopted lifecycle repair under regression. Seek current exact-version affected-device reproductions and comparable ordinary-play/sustained traces before making any further device or pacing claim. Preserve all five issues as open diagnostic boundaries, retain raw baseline/candidate evidence, and do not merge, tag, release, or advertise 0.16 performance without explicit authorization and real comparable measurements.
