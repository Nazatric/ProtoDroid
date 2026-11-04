# Phase 11 — Texture decode/upload, Bink media, and audio paths

Status: **active**. Started **2026-11-04T14:18:47.034587Z** from Phase 10 completion `e267295766f7dd66b72ac9c70039b90cf7b1517c`. Phase 10 is complete (finalizer 57/57; manifest 1,376 checks over 341 files); Phase 12 remains blocked until Phase 11 completes.

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

## Evidence plan

1. Freeze production/source/sample/tool hashes and regenerate the minimal decoder and sample without importing large artifacts into Git.
2. Run the unmodified production Bink UBSan suite at both audio rates before any candidate.
3. Build corrected decode and registered-plane harnesses with a dedicated guest stack, memory-backed input, output export outside timers, and exact per-frame digests/plane comparisons.
4. Use balanced baseline/A-A/candidate cohorts only for a source-led candidate; retain raw per-process results, absolute deltas, and cold/warm distinctions.
5. Run real-DLL audio differentials, media synchronization stress, broad regression, exact ARM64/Android CI, and APK packaging as applicable. Host/native CI is not physical-device FPS, playback, or sustained-performance proof.
6. Record every adoption/rejection and complete the seven-part report only after at least one hour of substantive work; use a longer budget while useful media work remains.
