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

If a redundant plane operation is source-established, implement the smallest semantics-preserving candidate and compare baseline/A-A/candidate in balanced fresh processes. Verify every frame and both plane sets, input immutability, strides/pitches, and normal/ASan+UBSan behavior. Reject neutral, immaterial, or output-changing changes; do not remove required registered copies or alter display timing.

### H3 — audio/mixer and media synchronization audit

Use the real-DLL differential harness and source inspection to rank mixer, ADPCM, biquad, audio-callback, and media-wait work. Any candidate must pass 32/44.1 kHz differentials and stress, preserve lead/wake behavior, and show material absolute cost in a realistic call shape. Do not infer phone underruns or A/V latency from host timing.

### H4 — texture decode/upload relevance and sustained media validation

Retain the Phase 5 DXT decoder unless a new exact workload exposes a material remaining path. Audit the boundary from decoded textures/Bink planes to D3D9 registration/upload without reducing texture quality, dynamic shadows, Bink, or mod compatibility. Finish with fresh broad regression, exact CI, explicit raw results, rejected alternatives, and physical-device limitations.

## Evidence plan

1. Freeze production/source/sample/tool hashes and regenerate the minimal decoder and sample without importing large artifacts into Git.
2. Run the unmodified production Bink UBSan suite at both audio rates before any candidate.
3. Build corrected decode and registered-plane harnesses with a dedicated guest stack, memory-backed input, output export outside timers, and exact per-frame digests/plane comparisons.
4. Use balanced baseline/A-A/candidate cohorts only for a source-led candidate; retain raw per-process results, absolute deltas, and cold/warm distinctions.
5. Run real-DLL audio differentials, media synchronization stress, broad regression, exact ARM64/Android CI, and APK packaging as applicable. Host/native CI is not physical-device FPS, playback, or sustained-performance proof.
6. Record every adoption/rejection and complete the seven-part report only after at least one hour of substantive work; use a longer budget while useful media work remains.
