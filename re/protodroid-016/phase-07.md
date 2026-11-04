# Phase 7 — memory, allocation, RCF/asset I/O, and streaming

Status: **active**. Started **2026-11-04T04:19:50Z** from Phase 6 completion commit `69f888334a474aa36db7f8cece0d68d78e6bcf0c` on `arena/5eb1c06c-protodroid`. No Phase 7 result or runtime change exists yet.

## Baseline and evidence scope

- Exact baseline copies of `kernel32.cpp`, `win.h`, `bink_media.cpp`, `path.cpp`, and `bink_test.cpp` are hash-verified under `/home/user/protodroid-016-evidence/phase-07/`.
- Phase 3 measured production-path RCF cold indexing and warm opens at an observed 18,047-path fixture scale. Its focused warm-open median was 12.519750 µs, with `Normalize`, `HostPath`, and hash lookup among the sampled costs. Those figures guide hypotheses but are not a new A/B baseline or phone result.
- Original game files, archives, binaries, logs, saves, and supplied fixtures remain read-only. New archives and outputs are generated outside Git. Cold means fresh process/index; warm means already-built in-process index. Neither is dropped-phone-storage cache.
- No physical Android device, ADB, low-memory killer, or Android storage profiler is available. Startup/streaming FPS and device memory pressure remain unmeasured.

## Initial hypotheses and correctness gates

1. **RCF exact-hit basename deferral:** `OpenArchiveSource` constructs `normalized` and then `base` before attempting the exact map lookup. Defer basename extraction until exact lookup misses. Hypothesis: remove one substring/allocation path from exact hits without changing exact/basename/RZ fallback order. Test mixed-case/slashes, `.`/`..`, basename collisions, compressed RZ, malformed archives, loose-file/mod priority, and missing content.
2. **Small multi-wait storage:** `WaitForMultipleObjects` heap-allocates its object vector for ordinary nonzero counts. Test bounded stack storage for common small counts with a heap fallback for arbitrary larger counts. Preserve null handles, duplicate objects, all/any acquisition, timeout cleanup, waiter registration order, and 64+ handle behavior. Reject if normal variance or added large-count cost dominates.
3. **Audit before other changes:** inspect RCF index ownership/copies and ordinary `File` path retention, but do not turn every allocation into a patch. Any cache must remain bounded and content/mod correct; no readahead/async I/O without lifetime and streaming evidence.

Every candidate needs an identical A/A control, rotated fresh-process cohorts, exact output/state differentials, normal-build measurements, and sanitizer coverage. Host nanoseconds are not device startup, streaming, FPS, or pacing evidence.

## P7-RCF — exact-hit basename deferral: rejected

The evidence-only candidate moved `base = normalized.substr(...)` inside the exact-miss branch, retaining basename, exact-RZ, then basename-RZ fallback order. It was measured before integration and has now been reverted; `src/native/bink_media.cpp` exactly matches its baseline SHA-256 `40497b69a6475ffe561789a905821dfa8ac7cfeba80bcdf6eed344c610f0b7a4`.

| Actual resolver cohort | Baseline median | Identical A/A control | Candidate | Candidate/baseline | Paired interpretation |
| --- | ---: | ---: | ---: | ---: | --- |
| Full `OpenFileSource` + production `path.cpp`, 36 rounds/arm × 20,000 opens | 12,008.622 ns | 12,012.6645 ns | 11,951.1335 ns | 0.995213 | Candidate paired median **+2.8035 ns**, 18 faster/18 slower; apparent aggregate gain is not robust |
| Direct `OpenArchiveSource`, 36 rounds/arm × 100,000 opens | 2,987.5725 ns | 2,987.121 ns | 2,976.3615 ns | 0.996247 | Candidate paired median −5.0785 ns versus A/A −7.926 ns; control is slightly better, so no candidate separation |

- **Raw work:** 108 full-path processes / 2,160,000 opens plus 108 direct-archive processes / 10,800,000 opens; all 216 processes returned zero and every open produced the expected payload digest `852fa1ad3927b57d`. Fixture and binary/source hashes, per-process JSONL, medians/ranges, and independently recomputed paired data are under `phase-07/rcf-*`.
- **Cold behavior:** the candidate does not touch index construction. Full-path cold medians were 20.460127/20.1884055/20.5046315 ms for baseline/control/candidate; no cold/start-up improvement is claimed.
- **Correctness differential:** baseline and candidate each passed the same 8/8 generated-fixture checks: two normalized exact forms, basename fallback, exact and basename RZ expansion, loose payload, loose-over-archive priority, and missing-content rejection. A malformed archive coexisted without preventing valid lookups. Outputs are identical. This does not replace full Bink decode/mod testing.
- **Decision:** **rejected as immaterial/noisy**. The deferral is semantically plausible, but the measured effect does not exceed the identical control and RCF movie opens are not established as a frame-loop hotspot. No runtime change is retained.
- **Environment/limits:** x86-64 GCC `-O2 -DNDEBUG`, production flat-memory/guest-base/signed-char flags, pinned zlib 1.3.1 (`51b7f2ab…`) built outside Git, fresh process per sample, 18,044-entry generated RCF. Host storage and synthetic opens are not Android startup, streaming, FPS, or pacing results.
- **Verification:** `verify_rcf_results.py` independently passed 30/30 raw/count/digest/median/paired/semantic/reversion checks. Initial missing-zlib/guest-flag and summary-category mistakes are preserved; neither produced a valid performance result.

Next: bounded small-count `WaitForMultipleObjects` storage, with actual synchronization semantics and large-count fallback checks before any timing disposition.
