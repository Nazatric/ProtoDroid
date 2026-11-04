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

## P7-WAIT — bounded inline multi-wait storage: adopted host candidate

`WaitForMultipleObjects` now uses eight inline `KObject*` slots and switches to a sized vector for larger counts. Resolution, `WaitKernel`, waiter order, duplicate/null handling, acquisition, timeout cleanup, and return ABI remain the actual production logic. Larger counts also avoid repeated vector growth by sizing once. Commit `2e9e31bb30f1158238865800471c675f57fac5cb` contains only `src/native/kernel32.cpp`; exact-code CI run `41773816427` is in progress at this checkpoint.

### Before/after measurements

Actual `kernel32.cpp` was compiled into focused baseline/A-A/candidate harnesses with only the import registry removed so unused handlers could be section-collected. The real handle table, guest `R32`, `HandleObject`, events, mutex, `WaitKernel`, acquisition, and stdcall cleanup executed. Timed calls use pre-signaled manual events and include the same ABI setup/result digest work in every arm. Sixteen rotated fresh-process rounds ran each of 14 count/mode cases, 200,000 calls per process: **672 processes / 134,400,000 calls**, all return code 0 and identical per-case digests.

| Handles | Mode | Baseline ns/call | A/A / baseline | Candidate ns/call | Candidate / baseline | Candidate faster rounds |
| ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | any | 49.322032 | 1.006873 | 33.133553 | 0.671780 | 16/16 |
| 1 | all | 49.022065 | 1.006233 | 33.693485 | 0.687313 | 16/16 |
| 2 | any | 69.802705 | 1.003580 | 34.019755 | 0.487370 | 16/16 |
| 2 | all | 75.459545 | 1.006420 | 39.250222 | 0.520149 | 16/16 |
| 4 | any | 90.567267 | 0.996388 | 35.937290 | 0.396802 | 16/16 |
| 4 | all | 101.809430 | 0.992549 | 48.215047 | 0.473581 | 16/16 |
| 8 | any | 113.510868 | 1.002308 | 40.977818 | 0.361003 | 16/16 |
| 8 | all | 137.276320 | 0.992196 | 68.955590 | 0.502312 | 16/16 |
| 9 | any | 133.424508 | 1.020659 | 54.148895 | 0.405839 | 16/16 |
| 9 | all | 158.087970 | 1.017945 | 84.215953 | 0.532716 | 16/16 |
| 16 | any | 141.435978 | 1.007035 | 62.221887 | 0.439930 | 16/16 |
| 16 | all | 189.869313 | 0.993306 | 113.135935 | 0.595862 | 16/16 |
| 64 | any | 276.394252 | 1.024925 | 119.948563 | 0.433976 | 16/16 |
| 64 | all | 454.739440 | 1.015428 | 354.375302 | 0.779293 | 15/16 |

A read-only static census of the original `prototypeenginef.dll` (SHA-256 `d331be205bdde99897952ee88261ab7e63c5c65e36a4660e2a367886a3537071`) found three direct import call sites. Address `10abdb6d` passes **n = 2, any, infinite timeout**; `10abbad6` and `10abbbc5` pass dynamic counts, any, infinite timeout. No direct call/jump to the import thunk was found. This establishes a real two-handle any-wait path, but not call frequency, dynamic-count distribution, or immediate-versus-blocking proportions.

These are host handler timings, not frame times. The candidate/baseline median range is 0.361003–0.779293 (about 22.1–63.9% lower handler time in these fixtures); no real game multi-wait frequency or physical-device FPS/pacing share is known.

### Allocation and correctness evidence

- Counting-only instrumented builds made 20 checked calls. Baseline allocation counts at 0/1/2/3/4/8/9/16/64 handles were 0/1/2/3/3/4/5/5/7; candidate counts were 0/0/0/0/0/0/1/1/1. The four-handle `all` control likewise changed from three to zero. This directly confirms the intended mechanism independently of timing.
- Baseline, A/A, and candidate each passed the same 98-check normal semantic suite with 64 blocking wakes and digest `1e49cb0cfd31e15d`. It covers zero/small/large counts, any/all, index selection, duplicates, null handles, auto/manual acquisition, timeout cleanup, and 9-entry heap cleanup.
- The extended actual-code suite adds 9- and 64-handle blocking wakes/cleanup. Thirty-two repetitions of each baseline/candidate normal, ASan+UBSan, and TSan variant passed: **192 processes, 24,960 checks, 18,432 blocking wakes, zero sanitizer stderr**, with identical 130-check digest `40815785c9f6425d` per run. ASan+UBSan uses a high test-only guest base because ASan reserves the production 64-GiB-base shadow gap; the initial failed reservation and layout evidence are preserved. TSan uses the production base.
- Independent verification passed 257/257 timing/raw/semantic/source checks and separately verified all stress counts, variant sums, digests, and empty logs. The broad repository regression passed **12/12 commands in 8.691055816s**, and `kernel32.cpp` compiled with production warning/flat-memory flags.
- **Disposition:** adopted as a host-validated runtime candidate. It removes allocations and has consistent actual-handler gains without a measured large-count or semantic tradeoff. Physical Android FPS, pacing, sustained scheduling, and real workload allocation rates remain unmeasured.

Performance timing does not exercise lock-contended wait latency; blocking tests are correctness evidence only. Close-versus-wait lifetime policy, mutex/thread/file-specific acquisition beyond the event cases, Android/Bionic allocator behavior, and engine call distribution remain outside this fixture. No APK or release is produced by this commit.
