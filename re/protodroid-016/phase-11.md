# Phase 11 — Texture decode/upload, Bink media, and audio paths

Status: **complete**. Started **2026-11-04T14:18:47.034587Z**; completion reading **2026-11-04T17:15:07.338209Z** (**10,580.303622 seconds**) from Phase 10 completion `e267295766f7dd66b72ac9c70039b90cf7b1517c`. Validation head is `b5c9e50043bd114520b9acc9dfee8fe65016aabe`; Phase 12 is next. Issues #22, #29, #32, #33, and #35 remain explicitly open rather than being relabeled as fixed.

## Exact source, fixtures, and settings

- Production baseline is `e267295766f7dd66b72ac9c70039b90cf7b1517c`; Phase 10 validation head `7c4b986d91ca0dbd0987c2679f720ab8733d6c08` differs only by completion documentation.
- Existing quality-preserving DXT direct32/palette work and all Phase 7–10 runtime dispositions remain intact. Do not reopen rejected RCF basename, Present, cache, or allocator changes without new workload evidence.
- Bink fixture: external `valve.bik`, 15,563,652 bytes, SHA-256 `e4cc59c1242615fc33159537e21d019599481676f1de62818a5cc4aa9d2f2dd5`; sample repository `dimhotepus/Bink-1-and-2-async-media-player` at pinned `48bd8df605e36f4c66d365acb9843ac77bb76cf5`. It remains outside Git.
- Decoder stack: FFmpeg n7.1.1 `db69d06eeeab4f46da15030a80d539efb4503ca8`, minimal static Bink/Bink-audio build without x86asm; zlib v1.3.1 `51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf`. Host timing is not Android playback, storage, GPU-upload, or FPS evidence.
- Correctness requires exact decoded output, unchanged compressed input, registered Y/Cb/Cr planes with padded strides and negative pitches, both 32,000 and 44,100 Hz audio behavior, A/V timing/lead semantics, and sanitizer coverage. No unconditional Bink/cutscene skip, frame skip, quality reduction, or audio-rate behavior change is allowed.

## Source-led hypotheses

### H1 — regenerate the real Bink decode/registered-plane baseline and attribute copy work

**Fresh baseline complete; no candidate adopted.** The unmodified production UBSan suite passed at both 32,000 and 44,100 Hz with empty stderr before profiling. The normal harness includes the production test translation unit and production Bink sources, reserves the guest call-stack region before allocating the input, and checks the complete 15,563,652-byte compressed input after every frame. `BinkDoFrame` is timed; input loading, open, YV12 export, hashing, input comparison, registered-plane comparison, and `NextFrame` are outside that timer. All 291 frames are compared by FNV-1a-64 per-frame hashes; registered runs additionally compare every visible Y/Cr/Cb row in both padded, negative-pitch plane sets against the exported YV12 frame.

The balanced cohort alternated which path ran first in each of 20 pairs, using a fresh process for every row. It decoded 11,640 frames in 40 processes. `verify_bink_baseline.py` independently passed **23,713 checks**, including every per-frame output hash, timing sum, combined digest, mode/order count, source/binary/sample hash, and all reported statistics. Raw per-frame arrays and stderr are under `phase-11/baseline/`; `bink-baseline-summary.json` and `bink-baseline-verification.json` are the canonical summaries.

| Per-run statistic; median of 20 fresh processes | Unregistered | Registered, two padded negative-pitch sets |
|---|---:|---:|
| Open (ms) | 8.725554 | 8.677609 |
| 291-frame `BinkDoFrame` total (ms) | 525.858246 | 650.740283 |
| Frame mean (ms) | 1.807073 | 2.236221 |
| Frame median (ms) | 1.089964 | 1.509854 |
| Frame p95 (ms) | 4.309956 | 4.730767 |
| Frame p99 (ms) | 4.857595 | 5.254400 |
| Frame maximum (ms) | 5.168086 | 5.890377 |
| Total range (ms) | 487.420738–581.712113 | 603.625217–675.917202 |

The paired registered/unregistered total ratio was **1.233614** median (range 1.142091–1.306304); the paired total delta was **125.535526 ms** median, approximately **0.431394 ms per decoded frame**. Percentiles are nearest-rank within each process and then summarized by their cross-process median, not pooled frame percentiles. The common output digest is `9e46ff3727b5d03a`; normal profile binary SHA-256 is `1ec4be42b103ba82aba239dd736d8080c8cb44e4bf5c9d6fbd815a0803f2d3c8`. This is fresh memory-backed x86-64 host evidence, not Android storage, real-time playback, GPU upload, or FPS evidence. The registered/unregistered delta includes production registered-plane behavior and is not pure GPU upload attribution.

Source attribution bounds the next step: `BinkDoFrame` publishes a decoded frame through `UploadRegisteredLocked`, and `CopyRegisteredFrame` validates and copies every populated set because an old set can still be unlocked by the guest. `CopyPlane` already uses one contiguous `memcpy` only for tightly packed matching signed pitches. The profiled negative/padded destinations require row-wise copies; padding and opposite row order are not redundant bytes that can simply be collapsed into one copy. Do not remove either populated-set copy or cache live plane spans without a trace proving the guest cannot use/move them.

### H2 — Bink plane/copy candidate only after exact attribution

**Complete: no plane-copy candidate.** A direct production `CopyRegisteredFrame` fixture used the same 1920×1080 YUV420P dimensions and source line sizes (1920/960/960), eight padded/tight × positive/negative × one/two-set layouts, 12 balanced rounds, and 1,000 timed copies per fresh process. The 96 processes executed 96,000 production copies; `verify_bink_plane_cohort.py` independently passed **97,086 checks**, and the harness verified **1,866,240 visible rows** plus source immutability, inter-row padding, and outer guards. Set-zero visible output digest is `d4cd313d58d1ba7d` for every layout/process.

| Layout | Mean ms/copy, one set | Mean ms/copy, two sets | Two/one ratio |
|---|---:|---:|---:|
| Padded positive | 0.234038 | 0.467849 | 1.988399 |
| Padded negative | 0.238684 | 0.479981 | 2.015003 |
| Tight positive (existing contiguous path) | 0.234730 | 0.471506 | 2.003289 |
| Tight negative | 0.234947 | 0.465063 | 1.971306 |

The padded-negative second set adds 0.240329 ms/copy median, consistent with required bytes dominating. For the primary padded-negative two-set case, per-run p95/p99 medians are 0.516423/0.569415 ms. Existing tight-positive two-set copies are **not faster** than padded-positive copies: tight/padded ratio 1.012608 with only 3/12 wins (p95/p99 0.506492/0.531360 ms). Negative tight/padded comparisons describe guest layout differences, not permission to alter guest pitches. The direct 0.480 ms copy cost is consistent in scale with H1's 0.431 ms registered-path delta but is a separate warm synthetic measurement, not an exact causal equality or GPU upload time.

Disposition: reject row-call elimination, padding overwrite, opposite-pitch block copies, live-pointer caching, or dropping a populated set. One-set rows are attribution controls only. No production source change is justified; required visible bytes and the two live sets dominate, while the existing contiguous path provides no measured material headroom.

### H3 — audio/mixer and media synchronization audit

Use the real-DLL differential harness and source inspection to rank mixer, ADPCM, biquad, audio-callback, and media-wait work. Any candidate must pass 32/44.1 kHz differentials and stress, preserve lead/wake behavior, and show material absolute cost in a realistic call shape. Do not infer phone underruns or A/V latency from host timing.

The first source-led candidate is limited to `AudioRing::At`: active callback reads use 64-bit `% capacity` for every sample/channel, although `i < count <= frames <= capacity` guarantees that `head + i < 2 * capacity`, so at most one subtraction is needed. The evidence-only fixture invokes the actual callback with one live movie/track, 1,024 stereo float frames, preallocated output, nontrivial gain/pan, and direct/bin plus wrapping/non-wrapping cases. An independent scalar reference checks every output bit and wrapped consumption outside the timer. Baseline smoke medians are approximately 12.18 µs per callback. Before candidate measurement, the adoption threshold is fixed at a robust win over baseline **and** identical A/A control, exact outputs at 32/44.1/48 kHz, and at least **0.25 ms CPU/s saved** at the corresponding real callback rate (with the relative callback reduction also reported). A ratio-only or smaller absolute win will be rejected; no phone underrun or FPS inference is allowed.

The full x86-64 cohort used 12 balanced rounds, four fresh-process labels (baseline, identical A/A control, conditional subtract, and contiguous-span read), five configurations, and 4,000 timed callbacks per process: **240 processes, 960,000 timed callbacks, and 967,680 bit-checked callbacks**. `verify_bink_audio_cohort.py` independently passed **962,723 checks** over the raw data. Direct and bin output digests are respectively `3cb3a9392356307a` and `479b664ec1a33ddf` for every label/rate/wrap case.

| Configuration | Baseline mean (µs/callback) | A/A ratio | Subtract/baseline | Span/baseline | Span saved (ms CPU/s) | Span wins |
|---|---:|---:|---:|---:|---:|---:|
| 48 kHz wrap direct | 12.176941 | 0.975426 | 1.028354 | 0.778143 | 0.126010 | 11/12 |
| 48 kHz nowrap direct | 12.512207 | 0.991792 | 1.025364 | 0.783329 | 0.126436 | 12/12 |
| 48 kHz wrap bin | 12.514118 | 0.988582 | 1.131031 | 0.780066 | 0.130193 | 11/12 |
| 44.1 kHz wrap direct | 12.484838 | 1.006120 | 1.089644 | 0.778607 | 0.118885 | 12/12 |
| 32 kHz wrap direct | 12.066680 | 1.004800 | 1.069786 | 0.793140 | 0.077946 | 12/12 |

**Conditional subtract is rejected:** it is slower in every configuration (median ratios 1.025–1.131); disassembly shows a loop-carried `cmov` chain. The span reader is genuinely faster, but every x86-64 absolute saving is below the preregistered 0.25 ms CPU/s threshold.

Native ARM64 CI run **41860945892**, job check **126682247381**, and API evidence check **126683105548** then measured the exact generated span source on `aarch64`: 180 fresh processes, 720,000 timed callbacks, and 725,760 bit-checked callbacks. The API-published compact summary passed 113 structural/source/hash/range checks. All span rows beat both baseline and identical control 12/12, but absolute savings remained below threshold:

| ARM64 configuration | Baseline mean (µs) | A/A ratio | Span/baseline | Span/control | Saved (ms CPU/s) |
|---|---:|---:|---:|---:|---:|
| 48 kHz wrap direct | 9.806117 | 1.000108 | 0.871390 | 0.870084 | 0.059141 |
| 48 kHz nowrap direct | 9.640136 | 1.001265 | 0.876797 | 0.874917 | 0.055683 |
| 48 kHz wrap bin | 9.752605 | 1.000016 | 0.904892 | 0.902891 | 0.043406 |
| 44.1 kHz wrap direct | 9.788919 | 1.000111 | 0.873595 | 0.874443 | 0.053298 |
| 32 kHz wrap direct | 9.817021 | 0.998515 | 0.869256 | 0.868675 | 0.039957 |

Even the published per-round saving maxima are below 0.25 ms CPU/s, so the rejection is not close. The signed artifact download returned EOF and was not retried; raw per-callback ARM64 arrays therefore could not be reopened, and this limitation is explicit rather than replaced with an independent-recompute claim. The API check retains per-label and paired summaries, exact source/binary/harness hashes, and correctness counts. **Both audio candidates are rejected on absolute materiality; production `bink.cpp` is unchanged.** The temporary job, portable repo harness, and measurement script are removed after preserving their exact content and CI patch outside Git. No phone underrun, A/V-latency, or FPS improvement is claimed.

### H4 — texture decode/upload relevance and sustained media validation

**Source audit complete; no texture/upload candidate.** `d3d9_dxt.cpp` remains the adopted Phase 5 decoder, SHA-256 `40c092c82ad0da0d606ea1da8c8a4bfbe041526b7b5e3b2bb16f2b1b0f038b1f`; no new workload justifies reopening direct32/palette-alpha or rejected packed16 forms. Writable adapted locks run mod replacement before `DecodeDxt`, then release compressed scratch after its consumers finish. Read-only locks do not write back, ordinary Play avoids the DXVK description query, disabled texture mods return before tracking/hash work, and dynamic textures are not in the static DXT adaptation path. The remaining `g_mutex` coverage around copies/decode is a possible serialization lead, not proof of concurrent game uploads; moving decode outside it would change lifetime/lock-order reasoning without a workload. Likewise, two relaxed diagnostic loads per unlock do not reopen Phase 10's rejected diagnostic-gating change. No scratch-retention, lock-lifetime, mod-bypass, quality, mip, or upload-policy change is adopted.

**Sustained host media validation complete.** One movie/process ran 50 complete loops at each forced output rate, with selected track 0, registered padded negative-pitch planes, YV12 export, and direct real-callback pumps. Across 32 and 44.1 kHz this covers **100 loops / 29,100 exact frames**, **29,100 full compressed-input comparisons**, **125,712,000 registered visible-row comparisons**, and **29,100 audio callbacks** (21,650 nonzero). Every loop reproduces fresh-baseline digest `9e46ff3727b5d03a`; every frame hash must equal the first loop and every wrap returns to frame 1. Independent log/memory verification passes **88 checks** with empty stderr. Validation wall time is 283.875685 seconds, not real-time playback or an FPS measurement.

| RSS/VM sample (KiB) | Initial | Loop 10 | Loop 20 | Loop 30 | Loop 40 | Loop 50 | After close |
|---|---:|---:|---:|---:|---:|---:|---:|
| 32 kHz RSS | 69,320 | 82,316 | 82,580 | 82,580 | 82,580 | 82,580 | 62,360 |
| 32 kHz VM size | 4,255,168 | 4,268,148 | 4,268,148 | 4,268,148 | 4,268,148 | 4,268,148 | 4,247,896 |
| 44.1 kHz RSS | 69,616 | 78,184 | 78,184 | 78,184 | 78,184 | 78,184 | 58,188 |
| 44.1 kHz VM size | 4,255,356 | 4,263,836 | 4,263,836 | 4,263,836 | 4,263,836 | 4,263,836 | 4,243,584 |

The bump-only test heap remains 29,228,960 bytes in every sample. RSS/VM are flat from loop 20 onward and close releases host memory; this detects no state drift in this workload but is not a proof of no leaks, physical Android memory pressure, underruns, A/V synchronization quality, or sustained device performance. H4 therefore closes with retained DXT/upload behavior and no production runtime change.

### H5 — real-DLL audio differential/cost and final broad regression

**Real-DLL audio evidence complete; no speculative mixer rewrite.** Twelve fresh processes reran the actual DLL differential harness and timed the existing native mixer for 168,000 calls total. Each process checks 260 mixer, 300 biquad, 200 ADPCM, and 2,000 skin trials; all 12 have zero differences. Across the cohort this is 3,120 mixer, 3,600 biquad, and 2,400 ADPCM differential trials, plus the separate skin control. The maximal synthetic mixer call uses three channels, three outputs, every bin, and 256 samples. Its output digest is `1b1304bebe7b5bc3` in every process; the median of process medians is **4.8505 µs/call** (process-median range **4.682–5.474 µs**). Independent raw-log recomputation passes **222 checks**. The benchmark times the existing native replacement, not a proposed candidate, and the exact differentials are correctness evidence. Without a measured game voice count/cadence or native ARM64 timing, this maximal host call cannot be converted into a real CPU/s or FPS share; no mixer change is adopted from synthetic cost alone.

**Fresh broad regression passes.** The corrected 13-group runner completed **53/53 commands in 108.336966312 s** at `bca15d8abf036d79dfa6c3029679d756cd2ee52e`, covering native launcher/input/runtime/memory, DXT, mods and scripting, diagnostics/pacing policy, patched DXVK structure, the real 1,014-entry cache, both production Bink audio rates, and real-DLL differentials. Independent replay passes **4,008 checks**, including all command/log hashes and byte counts, required result markers, source hashes, both Bink runs, every real-cache SHA-1 pair, and the real-DLL trial counts. The first stopped attempt remains preserved separately: 11 commands passed before plugin-loader command 4 failed because the runner used a custom directory although `mod_plugin_test.cpp` requires `/tmp/protodroid-plugin-test`. That harness invocation failure is not counted as passing or as a production defect; the corrected full rerun is the only passing regression. Local JDK/glslang remain unavailable, so Java and seven-shader compilation stay exact-CI gates. None of these host/native checks is physical-device FPS, rendering, pacing, or audio-device evidence.

### H6 — supplied correctness reports and affected-device evidence boundary

**Read-only investigation complete; no issue is claimed fixed.** The current bodies/comments for issues #35, #33, #32, #29, and #22 were retrieved, all supplied native/D3D9 logs were inventoried, all 44 supplied images were inspected through four labeled contact sheets, and the remaining `crash.log`, `journal.log`, `result.txt`, and `phase.txt` attachments were read. `crash.log` says that no crash was recorded; the other three files only repeat an isolated-tour D3D9 startup failure. `verify_phase11_issues.py` passes **72 checks** over the exact issue JSON, supplied-file hashes, settings, source revisions, and classifications. Its `fixed_issues` and `runtime_changes_selected` arrays are both empty.

- **#35 / Safe House and mission cutscenes:** the failing `fa8b6e55` direct-PanVK run uses 540p, two skipped mips, 512 shadows, and texture dumping; it reaches frame marker 8,192, then reports a watchdog at frame 10,634 with a `LockRect` stack and no mission Bink open. The matching D3D9 log later reports `VK_ERROR_DEVICE_LOST`. Crucially, the previous log has the **same source revision and those same settings**, reaches frame 65,536 without a watchdog, and opens mission Bink videos including `woi_node_093.bik` and `woi_ec2.bik`. This is a useful same-configuration control, not proof that PanVK, dumping, `LockRect`, Bink, or a synchronization change caused or will fix the failure. Do not unconditionally skip cutscenes, disable Bink, or rewrite locks from this correlation.
- **#33 / Adreno 642L:** the supplied `ea2ba6c9` logs are explicit system-Vulkan-1.1 rejection, not a fatal crash trace. A new comment at **2026-11-04T16:59:51Z** says the issue still occurs, but supplies no current native/D3D9 log or exact affected revision. The old diagnosis must not be carried forward as proof of the latest recurrence.
- **#32 / Adreno 610/PurpleVK:** the old `fa8b6e55` log ends at `VK_ERROR_FEATURE_NOT_PRESENT`. Its feature list is requested state and contains neither the newer requested-feature audit nor an exact missing-feature name. Capability spoofing, blanket feature disabling, or attributing the failure to a feature from another renderer is rejected.
- **#22 / Mali-G57:** `proto-native (3).log` and `proto-native-13.log` are byte-identical. The old `ea2ba6c9` D3D9 log shows resets/presents, BC decoding and dummy-resource context, but no logged device-creation or device-loss failure. The same broad fallback profile also appears on longer-running Mali-G720 logs, so it is not a G57-specific diagnosis. Existing 0.15 full-color-DXT1, wrapper, recovery, and diagnostic work remains intact but is not relabeled as a validated #22 fix.
- **#29 / Itel P55:** the report remains generic launcher/setup text with no actionable native/D3D9 log. No launcher, wrapper, Vulkan-version, or PanVK code change can be validated from it.

Disposition: retain all runtime behavior and keep #22, #29, #32, #33, and #35 open. A future patch needs a current affected-version reproduction or a source-provable defect with focused regression coverage. Host Bink stress, historical logs, successful CI, and image inspection are not affected-device visual, audio, crash, or sustained-playback validation.

## Evidence plan

1. Freeze production/source/sample/tool hashes and regenerate the minimal decoder and sample without importing large artifacts into Git.
2. Run the unmodified production Bink UBSan suite at both audio rates before any candidate.
3. Build corrected decode and registered-plane harnesses with a dedicated guest stack, memory-backed input, output export outside timers, and exact per-frame digests/plane comparisons.
4. Use balanced baseline/A-A/candidate cohorts only for a source-led candidate; retain raw per-process results, absolute deltas, and cold/warm distinctions.
5. Run real-DLL audio differentials, media synchronization stress, broad regression, exact ARM64/Android CI, and APK packaging as applicable. Host/native CI is not physical-device FPS, playback, or sustained-performance proof.
6. Record every adoption/rejection and complete the seven-part report only after at least one hour of substantive work; use a longer budget while useful media work remains.

## Phase 11 completion report

### 1. Actual time and work

Phase 11 started **2026-11-04T14:18:47.034587Z**; the completion reading is **2026-11-04T17:15:07.338209Z**, or **10,580.303622 seconds (2h56m20.303622s)** of substantive source audit, fixture construction, measurements, independent verification, regression, issue investigation, CI, and documentation. It began from Phase 10 completion `e267295766f7dd66b72ac9c70039b90cf7b1517c`; validation head is `b5c9e50043bd114520b9acc9dfee8fe65016aabe`, and exact production/CI validation head is `92b85d1b49c9ff8e4c1fca2e91da76d0fe1d5a59`. H1 regenerated the real Bink baseline; H2 measured production plane copies; H3 measured exact audio candidates on x86-64 and native ARM64; H4 audited DXT/upload paths and ran sustained media stress; H5 reran real-DLL and broad regression; H6 investigated all supplied issue evidence. No timer padding or physical-device claim is included.

### 2. Files and commits

Net Phase 11 production/runtime/workflow delta from Phase 10 is **zero**. Research documentation changed in `re/protodroid-016/phase-11.md`, `README.md`, `experiment-ledger.md`, `profiling.md`, `technique-matrix.md`, `rejected-changes.md`, `final-report.md`, and `device-results.md`. Important revisions are baseline `472caffe`, temporary exact ARM64 fixture `a691e7e8`, restored production/workflow identity and rejected candidates `92b85d1b`, sustained-media documentation `bca15d8a`, and final broad-regression/audio evidence `b5c9e500`; the completion commit is documentation-only. External raw logs, runners, binaries, verification reports, issue JSON, CI metadata, and the finalizer remain under `/home/user/protodroid-016-evidence/phase-11/`. The temporary ARM64 workflow/harness was removed after its exact source and evidence were preserved. No large artifact was added to Git.

### 3. Candidate dispositions

H1 is attribution only; H2 rejects row-call elimination, padding overwrite, opposite-pitch block copies, live-pointer caching, and dropping either populated plane set. H3 rejects conditional subtraction as slower and rejects the genuinely faster span reader because its absolute saving is below the preregistered **0.25 ms CPU/s** gate on both architectures. H4 retains the existing DXT decoder, lock lifetimes, uploads, media synchronization, and both audio-rate behaviors. H5 adopts no synthetic-cost mixer rewrite. H6 selects no speculative runtime patch and leaves issues **#22, #29, #32, #33, and #35 open**. Existing full-color DXT, Bink, dynamic shadows, AA/texture quality, mods, A/V behavior, safe fallbacks, and prior runtime dispositions are unchanged.

### 4. Raw before/after measurements

The balanced Bink baseline covers **40 processes / 11,640 frames**. Median 291-frame `BinkDoFrame` totals are **525.858246 ms unregistered / 650.740283 ms registered**, paired ratio **1.233614**, median delta **125.535526 ms** (about **0.431394 ms/frame**); common output digest is `9e46ff3727b5d03a`. Direct production copies cover **96 processes / 96,000 copies**: padded-negative one/two-set means are **0.238684268/0.479980856 ms**, and tight/padded two-set ratio is **1.0126077570** with tight winning only 3/12 pairs. No copy candidate survives.

The x86 audio cohort covers **240 processes / 960,000 timed callbacks**. Conditional-subtract ratios are 1.025–1.131 (slower); span reading saves only **0.077946–0.130193 ms CPU/s**. Native ARM64 covers **180 processes / 720,000 timed callbacks** and saves only **0.039957–0.059141 ms CPU/s**, despite 12/12 wins. The real-DLL cohort covers **12 processes / 168,000 timed mixer calls**, median of process medians **4.8505 µs/call** (range 4.682–5.474 µs), with no measured game cadence or FPS denominator. Sustained media covers **100 loops / 29,100 frames / 125,712,000 registered rows** in **283.875684564 s**; this is host decoding stress, not real-time Android playback or an FPS before/after result.

### 5. Correctness and regression evidence

The aggregate evidence gate recomputes or validates **1,088,025 checks**: 23,713 Bink baseline, 97,086 plane, 962,723 x86 audio, 113 compact native-ARM64 audio summary, 88 sustained-media, 222 real-DLL cohort, 4,008 broad-regression replay, and 72 issue-audit checks. All 291-frame outputs, compressed inputs, visible padded/negative-pitch planes, audio outputs, and loop digests match their references. Sustained media performs 29,100 full input comparisons and 29,100 audio callbacks (21,650 nonzero). The real cache remains **1,014/1,014 valid SHA-1 entries**. The corrected regression passes **13 groups / 53 commands in 108.336966312 s**; the preserved first attempt's plugin-path harness failure is not counted as passing or as a production defect.

CI run **41861964235** on `92b85d1b` is `completed/success`; `bink-tests`, dependencies, driver packages, native ARM64 graphics, Turnip, and `build-apk` all succeed, optional evidence checks are neutral, and release is skipped. The source-native build and signed-package verification steps pass while the prebuilt fallback is skipped. GitHub artifact **13341818959** is bound to the same run/revision, is 57,950,022-byte ZIP data, and has GitHub archive SHA-256 **`41f57621be39aab6ab21df660233bc6567f5773f5792ab688c2d813cf4880fed`**. This is the artifact ZIP digest, **not an inner-APK digest**.

### 6. Limitations

No ADB, affected physical device, Android/Bionic execution, GPU profiler, or local Android NDK is available. Host/native-CI results are not phone FPS, frame-time/pacing, GPU-upload, shader-loading, audio-device, A/V-quality, thermal, or sustained-playback proof. The ARM64 signed audio artifact remained unavailable, so its compact API summary is not described as per-callback replay. Local JDK/glslang/SPIR-V tools remain exact-CI coverage. The APK payload download ended with EOF after GitHub's signed blob redirect; no local APK/library hash, installation, or device-rendering claim is invented. The first local finalizer attempt also exposed and corrected verifier type/cleanup defects before the passing gate; no failed check was hidden.

Most importantly, the supplied issues are **not resolved by this evidence**. #35 has only correlational PanVK/dumping/`LockRect`/watchdog/device-loss evidence plus a working same-settings previous control; #33's latest recurrence lacks logs; #32 lacks the exact unsupported feature; #22 lacks a visual root cause; #29 lacks actionable logs. Diagnostics and existing compatibility work are not presented as fixes.

### 7. Next priority

Start **Phase 12** with sustained whole-program/memory/frame-stall research and explicit long-soak comparability controls, while carrying the five issues forward as open correctness work rather than silently closing them. Preserve the zero-change Phase 11 media dispositions unless new workload evidence justifies reopening one. Any affected-device fix still requires current exact-version logs or a source-provable defect and focused correctness coverage; no merge, tag, release, quality reduction, cutscene skip, or fabricated performance result is authorized by this completion.
