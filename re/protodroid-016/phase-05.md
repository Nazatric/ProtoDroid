# Phase 5 completion report — native CPU hot paths and vectorization

Status: **complete for available host/native hot-path research after 3,637.516840 seconds (1h00m37.516840)**. Direct32 + palette-alpha is adopted and exact-code CI tests are green. No physical-device FPS, frame-pacing, sustained-performance, packaging-completion, or release claim exists.

## 1. Actual time and work

- Started: `2026-11-03T21:29:04Z`, from Phase 4 completion commit `a5d4d2436bbf77f23a333abe9992ae3934464200`.
- Completion clock reading: `2026-11-03T22:29:41.516840Z`; cumulative active phase time **3,637.516840 seconds (1h00m37.516840)**. No idle-time or elapsed-time padding.
- Initial work: preserved exact source hashes/copies and inspected the production DXT and native audio paths plus their differential/benchmark fixtures.
- Phase 4 is complete after 1h00m01s. Its rejected dispatch layouts are not carried forward.

## 2. Exact baseline and environment

- ProtoDroid baseline for Phase 5 A/B: `a5d4d2436bbf77f23a333abe9992ae3934464200`; released 0.15 remains `18ba66bd0945e405aaded0de5ff8848a8cd07932`.
- `d3d9_dxt.cpp` SHA-256 `39dc85ff388bc405c839d300554687ac6cf48dc5a378c7d60fe20384b585e80c`; `hot_audio.cpp` SHA-256 `8b78ce8d547c83fb5c4ce4b6d317b2ec4fb078f2efd2b682f636475e0a2692f7`; `hot_skin.cpp` SHA-256 `58385ba17567a793bde65e99f10b3f47ab889f420fe2051a4e68403aa434204c`.
- Host: x86-64 Linux, two logical CPUs, GCC and isolated Zig/LLVM available. No physical Android device, ADB, device GPU profiler, or FPS measurement is available.
- Persistent Phase 5 evidence: `/home/user/protodroid-016-evidence/phase-05/`. Generated candidates, binaries, raw logs, and large outputs remain outside Git.

## 3. Evidence-led hypotheses

1. **DXT3/5 BGRA direct rows:** the Phase 3 destination rotation showed substantial DXT3/DXT5 host sensitivity while DXT1 was near unity. Current DXT1 already selects palette colors directly into destination rows; DXT3/5 still materializes a 64-byte tile and performs per-pixel stores on x86. A direct color+alpha row path may remove temporary/store overhead without changing arithmetic.
2. **Packed16 must be separate:** ARM64 currently has NEON full-tile narrowing. A direct 16-bit palette path may help or may lose useful vector stores, so it requires an independent hypothesis, differential, and target assembly review.
3. **Store-only control:** where practical, separate row-store changes from direct-palette changes to identify the actual mechanism rather than attributing all movement to cache blindly.
4. **Native mixer is lower priority:** the real-DLL mixer/biquad/ADPCM/skin differential is validated. Any audio candidate must preserve per-operation floating-point association and output digests; informational mixer timing alone is not a 0.16 gain.

## 4. Required experiment protocol

- Compare exact baseline/candidate sources with the same harness, flags, scene data, mutation sequence, and cache definition. Use interleaved repeated processes and retain every result.
- Benchmark multiple sizes and destination working-set slots; do not extrapolate one 512-square host result into a phone frame denominator.
- Compare every output byte and guard bytes over varied formats, dimensions, source/destination offsets, padded and negative pitches, edge blocks, alpha modes, and output formats. Run ASan+UBSan differential coverage as well as the production graphics tests.
- Inspect AArch64-musl production-like assembly for adopted candidates; instruction counts are structural only. Do not weaken floating-point semantics, alignment handling, or TSO globally.
- Adopt only output-equivalent changes whose gains exceed observed run-to-run variance without material regressions in another required path. Otherwise record reject/inconclusive and move to the next hypothesis.

## 5. Initial limitations and first action

No game-scene share, Android cache behavior, device FPS, frame-time, thermal, or sustained-pacing outcome is known. Host gains will remain explicitly labeled microbenchmark results. Next: establish fresh exact-commit DXT baseline runs and build the direct-row/store-only candidates plus full-output differential harness.

## 6. Provisional direct32 findings

- Baseline and candidates use exact sources/binaries under `phase-05/`; initial direct32 source SHA-256 is `62d17bea4e4eaabf26f2d2e0827d825137100cf51aee6565999dd1b30f13787e`. No candidate had been applied to production at that provisional checkpoint.
- Both direct32 and the row-store-only control passed the production graphics UBSan suite and all four existing benchmark digests. A two-implementation differential then passed **9,480 cases / 19,214,680 compared bytes** per candidate in both normal and ASan+UBSan builds. It covers DXT1–5, BGRA/A1R5G5B5, edge sizes, source/destination offsets, padded/negative pitches, alpha modes, complete destinations, padding, guards, and source immutability.
- The initial 36-process rotated screen gave direct32/baseline paired median ratios **0.714322 DXT3 BGRA** and **0.733313 DXT5 BGRA**. The row-store-only control was 1.011664 and 0.898944 respectively with wide paired ranges; it is superseded by the stronger direct path rather than adopted.
- A larger seven-configuration, 12-repetition, three-layout sweep completed **252 processes / 1,512 rows**. Independent verification matched every row to its process log, checked 504 digest trios, and recomputed 252 summary groups. The outer wrapper's unexplained status 1 after the final summary is preserved; it is not rewritten as success, and artifact validity comes from the independent checks.

Direct32/baseline paired medians:

| Configuration | DXT3 BGRA | DXT5 BGRA | DXT3 packed16 | DXT5 packed16 |
| --- | ---: | ---: | ---: | ---: |
| 64x64, 1 slot | 0.732336 | 0.763319 | 0.956408 | 0.982798 |
| 256x256, 1 slot | 0.720072 | 0.705982 | 1.018547 | 0.976937 |
| 512x512, 1 slot | 0.720096 | 0.747772 | 0.982738 | 0.995433 |
| 1024x1024, 1 slot | 0.731644 | 0.755725 | 1.006566 | 0.969668 |
| 512x512, 4 slots | 0.707137 | 0.745940 | 0.968369 | 0.993160 |
| 512x512, 16 slots | 0.717578 | 0.738223 | 0.994757 | 1.006398 |
| 512x512, 32 slots | 0.738566 | 0.747365 | 0.997397 | 0.980698 |

The BGRA result is a substantial, repeated host gain across sizes and destination working sets. Packed16 medians remain near unity but paired ranges are wide, so no packed16 optimization or regression claim is made. Unchanged DXT1 controls also vary and must be checked in the final confirmation.

The noinline control is generally slower than inline for DXT3 (most noinline/inline paired medians about 1.03–1.06) and mixed for DXT5/packed16. It is not the preferred integration form. Text-size and symbol evidence is retained; code layout is not treated as a proven cause of timing variation.

Next: a combined same-process baseline/candidate A/B with alternating batch order, followed by AArch64 assembly inspection and final integration regression if inline direct32 remains robust.

## 7. Same-process direct32 confirmation

The corrected dual harness links baseline and candidate into one binary, alternates which decoder runs first per batch, and verifies complete source/destination equality plus sampled digests after every batch. Twenty-four fresh processes ran 512x512 with 1 or 16 destination slots, 14 batches and 100 iterations per case: **2,016 timed batch pairs**, all output-equal. The even-batch median error in the initial smoke harness was corrected before this cohort and both smoke versions are preserved.

| Slots/case | Baseline µs (median of process medians) | Direct32 µs | Process paired ratio | Range |
| --- | ---: | ---: | ---: | --- |
| 1, DXT3 BGRA | 474.059580 | 338.085153 | 0.715007 | 0.683248–0.737633 |
| 16, DXT3 BGRA | 496.642108 | 357.127720 | 0.722852 | 0.713618–0.736837 |
| 1, DXT5 BGRA | 567.194520 | 419.574920 | 0.732585 | 0.726746–0.757247 |
| 16, DXT5 BGRA | 577.801132 | 438.012223 | 0.750275 | 0.735380–0.759216 |
| 1, DXT3 packed16 | 635.012845 | 634.606763 | 0.996151 | 0.982623–1.027785 |
| 16, DXT3 packed16 | 684.295687 | 689.470743 | 1.008014 | 0.978632–1.015628 |
| 1, DXT5 packed16 | 721.334350 | 702.608715 | 0.982676 | 0.976345–0.993253 |
| 16, DXT5 packed16 | 795.733665 | 786.874085 | 0.988832 | 0.983567–1.011337 |

Unchanged DXT1 controls are not uniformly neutral: DXT1-32 paired medians are 1.007792 (one slot) and 1.009869 (16 slots), while DXT1-16 is 0.957879 and 0.968701. This is a small code-generation/layout tradeoff, not proof that the direct DXT3/5 algorithm changes DXT1 semantics. It must be considered rather than hidden behind the large BGRA gain.

Direct32 remains **provisional**. Next: test a late-defined noinline helper as a code-layout control, inspect baseline/candidate AArch64 assembly, and choose between the simple inline gain and any lower-trade integration form using the complete evidence.

## 8. Rejected integration and alpha refinements

Both alternatives used the same 24-process, 2,016-output-equal-batch-pair dual protocol. Late noinline moved the helper after `DecodeDxt`; lazy alpha removed the 16-pixel alpha temporary and evaluated identical alpha arithmetic at each pixel. Lazy alpha also passed 9,480 cases / 19,214,680 compared bytes in normal and ASan+UBSan differential builds.

| Slots | Variant | DXT3 BGRA | DXT5 BGRA | DXT1-32 control | Disposition |
| --- | --- | ---: | ---: | ---: | --- |
| 1 | late noinline | 0.752706 | 0.725874 | 1.029062 | Reject: larger DXT1 regression and slower DXT3 than inline |
| 16 | late noinline | 0.748908 | 0.735248 | 1.028423 | Reject |
| 1 | lazy alpha | 0.751843 | 0.754922 | 0.994004 | Reject: primary BGRA paths slower than inline |
| 16 | lazy alpha | 0.763256 | 0.772249 | 0.988960 | Reject |
| 1 | original inline | 0.715007 | 0.732585 | 1.007792 | Selected |
| 16 | original inline | 0.722852 | 0.750275 | 1.009869 | Selected |

Lazy alpha's DXT1-16 ratios (0.916563/0.934172) are incidental unchanged-path layout effects; they do not justify sacrificing the target DXT3/5 gains or claiming an algorithmic DXT1 optimization. The earlier early-position noinline and rowtile32 forms are also rejected.

## 9. AArch64 structural inspection

Isolated Zig/LLVM compiled baseline and original direct32 for `aarch64-linux-musl`, `-mcpu=cortex_a76+rcpc -O3`, matching the DXT object's optimization level. Static `DecodeDxt` counts are:

| Source | Instructions | Stack bytes | `ldr`/`str` | `ldp`/`stp` |
| --- | ---: | ---: | ---: | ---: |
| Baseline | 1,207 | 320 | 117 / 72 | 15 / 23 |
| Direct32 | 1,684 | 368 | 188 / 94 | 10 / 33 |

The candidate adds 477 AArch64 instructions (1,908 bytes at fixed instruction width) and 48 stack bytes. This is a target-risk/code-size limitation, not evidence of an ARM regression or improvement. There is no local AArch64 execution or cycle/FPS inference. The rejected `-mtune` invocation and two analysis-parser failures are preserved; neither produced a target result.

## 10. Direct32 production integration (intermediate)

The selected source is now `src/native/d3d9_dxt.cpp`, SHA-256 **`a14f0e58bca272867a35c61a2693f08253341766c4cce1e3cc3396bb0ca233a5`**. It differs from the long-timed `62d17bea…` evidence source only by replacing the evidence comment with a production comment. Both compiled `.text` sections have SHA `44e64ad42f18831f6e504b27faf2525e1f3466a26ff298fa32b57b6133509033`.

- Exact final-source differential: **9,480 cases / 19,214,680 compared bytes passed normally and under ASan+UBSan**, digest `71c8a9f85b149abb`.
- Actual production-source broad native/mod/script/graphics/performance-policy regression: **all commands passed in 65.074660 seconds**.
- Production benchmark digests match baseline: **389410, 781134, 1139944, 1497235**.
- Independent verification matched all **72 dual-process logs, 432 result rows, 6,048 batch pairs, and 36 summary groups** across inline, late-noinline, and lazy-alpha experiments.
- One final isolated production benchmark decoded DXT3/DXT5 BGRA in 348.436/412.483 µs; these standalone timings are not the controlled A/B evidence and are not phone FPS.
- Initial `/usr/bin/time` and preservation-wrapper failures ran no suite; they are preserved separately from the successful Python-monotonic execution.

Disposition: **adopted for the 0.16 candidate after host correctness/integration checks**. Full native linking remains dependency-blocked; Android/Bionic execution, device FPS, frame pacing, and sustained thermal behavior remain unmeasured. Next: independent artifact verification, diff/CI checks, and the guarded phase-time gate.

## 11. Direct packed16 increment rejected

A separate increment attempted to decode DXT2–5 directly into A1R5G5B5, precomputing four packed colours and applying the established one-bit alpha threshold per pixel. Direct-all passed the full normal and ASan+UBSan 9,480-case differential, and every incremental dual smoke was output-equal. However, all four integration forms materially regressed the already selected BGRA path in 64x64 alternating smokes:

| Variant | DXT3 BGRA / direct32 | DXT5 BGRA / direct32 | DXT3 packed16 | DXT5 packed16 |
| --- | ---: | ---: | ---: | ---: |
| direct-all, both helpers inline | 1.682664 | 1.474472 | 0.808469 | 1.128788 |
| direct-all, noinline16 | 1.631414 | 1.376156 | 0.868882 | 0.901567 |
| minimal direct32+noinline16 | 1.608820 | 1.605351 | 0.794825 | 0.898887 |
| explicit BGRA plus generic fallback | 1.673543 | 1.804560 | 0.885117 | 0.897596 |

These are screening results, not long-cohort medians; the repeated 38–80% BGRA regressions are sufficient to reject without spending a long cohort on a bad integration. The exact cause is compiler code-generation sensitivity, not a claim that direct16 arithmetic is inherently slower. All direct16 forms are rejected and none entered production. A partial hash-manifest filename failure was preserved and corrected without rerunning any test.

## 12. Final palette-alpha simplification adopted

The four-colour palette path previously interpolated alpha even though endpoints and all resulting alpha values are 255. The final increment computes only three colour channels and explicitly stores alpha 255. This applies to DXT1 and the generic packed16 path as well as direct32, with no arithmetic change to colour or per-pixel DXT3/5 alpha.

The incremental same-process A/B uses **direct32 as baseline**, 24 processes, 1/16 slots, and 2,016 fully output-equal batch pairs:

| Case | 1 slot | 16 slots | Interpretation |
| --- | ---: | ---: | --- |
| DXT1 BGRA | 1.014120 | 1.013846 | Small consistent regression, retained as a tradeoff |
| DXT1 packed16 | 0.982783 | 0.990637 | Slight improvement |
| DXT3 BGRA | 0.994153 | 1.002547 | Approximately preserved |
| DXT3 packed16 | 0.921225 | 0.933343 | Material improvement |
| DXT5 BGRA | 0.986819 | 0.979198 | Improvement |
| DXT5 packed16 | 0.957948 | 0.963863 | Improvement |

Correctness and target checks:

- Exact candidate differential passed **9,480 cases / 19,214,680 compared bytes normally and under ASan+UBSan**.
- AArch64 `DecodeDxt` remains **1,684 instructions / 368 stack bytes**, identical to direct32; the assembly diff contains only two alpha-store ordering swaps.
- Final actual-source broad regression passed in **64.301764 seconds**; benchmark digests remain **389410, 781134, 1139944, 1497235**.
- Final independent verification covers **96 dual-process logs, 576 result rows, 8,064 batch pairs, and 48 summary groups** across inline, late-noinline, lazy-alpha, and palette-alpha experiments.

A further incremental palette/direct32 sweep ran **42 processes / 3,528 output-equal batch pairs** across all seven earlier size/slot configurations; all 42 logs, 252 result rows, and 42 summary groups were independently recomputed.

| Configuration | DXT3 BGRA | DXT5 BGRA | DXT3 packed16 | DXT5 packed16 |
| --- | ---: | ---: | ---: | ---: |
| 64x64, 1 slot | 0.988663 | 0.990931 | 0.918043 | 0.889590 |
| 256x256, 1 slot | 1.005069 | 0.991801 | 0.922553 | 0.946899 |
| 512x512, 1 slot | 0.982110 | 0.976225 | 0.918523 | 0.956166 |
| 1024x1024, 1 slot | 0.983692 | 0.985483 | 0.928380 | 0.942950 |
| 512x512, 4 slots | 0.988793 | 0.980706 | 0.936652 | 0.963885 |
| 512x512, 16 slots | 0.998334 | 0.982375 | 0.934777 | 0.962803 |
| 512x512, 32 slots | 1.011548 | 0.991285 | 0.933816 | 0.958923 |

Packed16 gains persist at every configuration. BGRA remains close to direct32; the 32-slot DXT3 ratio is a small regression rather than a uniform win. DXT1-32 remains about 1–1.6% slower at larger sizes. These are host decode ratios only.

Final production source: `src/native/d3d9_dxt.cpp`, SHA-256 **`40c092c82ad0da0d606ea1da8c8a4bfbe041526b7b5e3b2bb16f2b1b0f038b1f`**. **Direct32 + palette-alpha is the final Phase 5 candidate.** It retains the large DXT3/DXT5 BGRA host gain, improves generic packed16 decoding, and preserves all tested output. No Android execution, FPS, frame-pacing, or sustained-device claim is made. Remaining work is final diff/manifest/CI and the one-hour phase gate.

## 13. Exact-code CI and evidence limits

Code commit **`77d0c7c2bb8485335ba843960b4742bef745b8ef`** was pushed to draft PR #38. On PR run `41729459844`, `bink-tests` (52 steps), `arm64-graphics-tests` (25 steps), driver-package tests, dependencies, and Turnip all report success. ARM64 graphics UBSan and native hot-replacement differential steps passed explicitly. The downstream `build-apk` job was still in progress when test validation was recorded; it was not cancelled, but packaging completion is not claimed or required for this native-hot-path phase.

The ARM64 BC1 benchmark artifact uploaded successfully, but ZIP retrieval failed at GitHub's `productionresultsa2.blob.core.windows.net` redirect with EOF because that host is not reachable from the sandbox. The zero-byte failed download is preserved; **no ARM timing or artifact digest is available**. The workflow reference `29654c7d…` also differs from the Phase 5 baseline because it predates existing DXT1 direct paths, so even a retrieved DXT1 comparison would not have been an exact Phase 5 A/B. No benchmark result is inferred from upload success.

The actual Makefile object rule also compiled the final source successfully with shared `-O2` followed by the required `-O3` and source revision `77d0c7c…`; no full link was attempted because its dependencies remain absent. The final real-DLL hot differential also passed: mixer 0/260, biquad 0/300, ADPCM 0/200, and skin 0/2000 differing trials with engine SHA `d331be205bdde99897952ee88261ab7e63c5c65e36a4660e2a367886a3537071`. The initial isolated-Python/Capstone and default-DLL-path failures ran no differential and are preserved; the successful run used the evidence-only script copy, explicit package path, and explicit original engine. Its informational mixer timing is not an adopted result.

Remote scope checks also pass: PR #38 remains draft/open at the exact code commit, remote `main` remains `18ba66bd0945e405aaded0de5ff8848a8cd07932`, and the 0.15 release still exposes its 59,023,246-byte `prototype.apk` asset. No merge, tag, release, or replacement download was created.

The guarded local preflight passed 14 source/evidence checks and then correctly refused completion at **2,980.545414 seconds**. A later full dry run passed 20 local/CI checks and also correctly refused at **3,368.791262 seconds**. The real finalizer then passed **24 checks** at **3,637.516840 seconds**, including exact source/scope, released-main/PR/release preservation, all differential/cohort integrity, final regression, Makefile object, real-DLL hot differential, and CI/artifact-limit checks. `phase5-final-verification.json` records the passing gate; Phase 5 is complete and Phase 6 scheduling work is next.
