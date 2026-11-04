# ProtoDroid 0.16 final performance report

Status: **not final**. Phases 1–13 are complete within their recorded source/host/native-CI scopes; Phase 14 final evidence/release-readiness audit is next. The released baseline remains 0.15 `18ba66bd0945e405aaded0de5ff8848a8cd07932`. Phase 13 production/CI validation head is `2dabecdf3b15b14da56803863bb13b67c80070d6`; the completion documentation is layered on that head. No 0.16 merge, tag, release, or final performance claim is authorized.

Earlier adopted work remains intact: quality-preserving DXT direct32/palette decoding, bounded multi-wait storage, anisotropy initialization, byte-validated unchanged-cache-backup skipping, native launcher/controls, mods/metadata, texture modding, ARM64 ABI, safe defaults/drivers, dynamic shadows, Bink/cutscenes, A/V synchronization, both audio-rate behaviors, and the diagnostic tour. Phase 12 adds one source-provable **XAudio2/FAudio master-lifetime correctness fix**. Phase 13 adds a **Bink failed-output retry** with permanent negative/positive coverage. Neither is a measured FPS optimization or affected-device issue diagnosis.

## Phase 12 result

The old bridge could lose track of a real FAudio mastering voice when the void XAudio2 2.3 destroy call was refused because another voice still sent to it. The adopted change in `c8709cb` tracks `live_master`, uses `FAudioVoice_DestroyVoiceSafeEXT`, preserves the old real master on refusal, retries replacement only when no logical mastering users remain, and serializes mastering operations in both reuse modes. The permanent four-mode regression and pinned-FAudio CI coverage are included.

Fresh raw controls show the before/after correctness difference:

- Unpatched real FAudio/SDL controls compile in all four reuse/quantum modes and each exits 1 at check 23, `dependent-send replacement returns refusal`.
- Current real backend controls pass **43 checks in each of four modes**, with `old_master_preserved=1`, `detach_retry=1`, and empty sanitizer stderr.
- Current fake-backend regression passes **47 checks in each of four modes**, with zero master overwrites. Reuse records master create/destroy **2/1** and one refusal; non-reuse records **2/2** and two refusals.
- Fresh normal-lifecycle fixtures complete **32 source/master recreation cycles plus orphan cleanup** in each reuse mode: **502 checks reuse=1**, **470 checks reuse=0**, both sanitizer-clean.

These are host fake/dummy-audio lifecycle results. They do not establish audible quality, Android callback latency, FPS, frame-time, thermal, or a shared trigger for any supplied phone report.

## Sustained and performance evidence boundary

The fresh available-log audit passes **21 checks** over only two short historical diagnostic tours (61.544/67.945 seconds; 3,131/3,007 Present intervals; worst intervals 1,032.764/3,430.826 ms). The longer historical run raw inputs are unavailable. Their preserved summaries—run A 902.58 seconds/181 windows/53 pipelines/41,954 intervals and run B 521.98 seconds/105/11/37,105—remain summary-only. RSS/virtual growth and interval correlations do not prove leaks, causation, or a safe allocator/presenter/queue change.

No new physical-device FPS, frame-time p50/p95/p99, long-stall, cold/warm shader-loading, GPU-time, thermal, sustained-play, or A/V-quality measurement exists. Earlier host/native measurements retain their original scopes and are not promoted to phone results. No performance before/after number is invented for the lifecycle repair.

## Regression and packaging

The fresh broad regression at `c8709cb` passes **61/61 commands across 11 groups in 123.423438 seconds**. Independent raw-evidence verification passes **605 checks**, and the independent Phase 12 pre-commit finalizer passes **407/407 checks** after correction of its own porcelain-status whitespace parser; the initial verifier failure is preserved. The regression covers launcher/policy, diagnostics/settings, DXVK policy, mods, native core, fake and real XAudio2, Bink at 32/44.1 kHz, real-DLL differential, and reproducibility gates. The real cache remains **1,014/1,014 valid entries**. Historical expanded/graph/detach/threaded Phase 12 controls are retained as labeled summaries, not represented as freshly reopened raw files.

Exact CI run **41900484812** on `c8709cb5a553b470f8724dce15faae0779a85528` is **completed/success**. Required dependency, native, ARM64, driver, Turnip, and APK jobs succeed; optional ARM64 evidence jobs are neutral; `release` is skipped. The new pinned-FAudio checkout and four-mode XAudio2 regression pass in `bink-tests` steps 8–9. `build-apk` source build step 18, APK package/sign step 26, and APK/embedded-DXVK verification step 28 pass; the prebuilt-symbol fallback step 22 is skipped.

Artifact **13359379211** (`prototype-apk`) is server-described as **57,955,156 bytes** with archive digest `sha256:f3863a3f1a946fc72d0dfeaae128913231d600b537b3d948f02974862255a6bd`. This is GitHub artifact metadata, not a locally recomputed inner-APK digest. The signed blob redirect returned EOF, so no local APK/library hash, installation, or device-rendering claim is made.

## Supplied correctness reports

**#22, #29, #32, #33, and #35 remain open and unfixed by this program.** #35 has a working same-revision/settings control alongside a failing watchdog/`LockRect`/device-loss run; #33's latest recurrence lacks current logs; #32 lacks the exact unsupported feature; #22 lacks a visual root cause; #29 lacks actionable logs. The source-provable host XAudio2 defect is not promoted to a diagnosis for Dana's Safe House/cutscenes, Motorola Play-to-home, the Itel setup report, or the Mali visual glitch. No feature spoofing, unconditional cutscene skipping, quality reduction, universal driver workaround, or speculative lock rewrite is selected.

## Final work still required

The final version of this report still requires comparable exact-revision device/APK provenance, device/driver/settings coverage, median FPS and frame-time p50/p95/p99/long-stall changes, cold/warm shader-loading results, sustained/thermal behavior, visual/media/correctness validation, and explicit unmeasured outcomes. Phase 14 should preserve both adopted lifecycle regressions while consolidating final evidence and seeking current affected-version reproductions and comparable ordinary-play/sustained measurements. Empty or unavailable evidence remains explicit rather than replaced by estimates. No release-readiness conclusion is made by Phase 13 completion.


## Phase 13 interim result

Bink failed-output retry is adopted in `2dabecdf3b15b14da56803863bb13b67c80070d6` for host-reproducible recovery correctness only. The fresh broad regression passes 62/62 commands; concurrent, integrated, both-rate, and exact XAudio2 controls pass. Exact CI run `41911558127` succeeds, including source-built ARM64/APK verification; release is skipped. Artifact `13364468289` metadata is 57,950,912 bytes with archive SHA-256 `0a980739e8fb05c5dabd8ff48cffb4abff3ab33e66b8cb7cf1d5d060edc2f1e1`. No FPS, physical-device, audible-output, or supplied-issue fix is claimed. Phase 14 is next.
