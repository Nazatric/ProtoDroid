# Phase 6 — game loop, scheduling, locking, and CPU/GPU overlap

Status: **complete: available host/source research; no Phase 6 runtime optimization adopted**. Started **2026-11-04T03:19:00Z** from `00976d939e0cba01af908a8aecd4a02db6d41066`; completion reading **2026-11-04T04:19:16.064254Z**, elapsed **3616.064254 seconds** on `arena/5eb1c06c-protodroid`. Physical-device FPS/pacing remains unmeasured.

## Baseline and environment

- Released 0.15 baseline remains `18ba66bd0945e405aaded0de5ff8848a8cd07932`; the Phase 5 direct32 + palette-alpha code is already present in the merged baseline for this phase.
- Host: two logical Intel Xeon CPUs, 3.8 GiB RAM, GCC and binutils available. No ADB, physical Android device, simpleperf, Perfetto, GPU profiler, or Android/Bionic execution environment is available.
- Exact baseline copies and SHA-256 manifests are under `/home/user/protodroid-016-evidence/phase-06/baseline-*` and `baseline-source-hashes.txt`. Originals and supplied files remain read-only.
- Existing policy already avoids forced host affinity, reports the available CPU mask to the engine, uses ADPF where supported, and anchors pacing statistics to successful Presents. These are baseline behaviors, not new improvements.

## Evidence-led audit

`src/native/win.h::KObject::Notify()` currently calls `notify_one()` for every registered waiter. The four production notification sites are thread completion, `SetEvent`, `ReleaseSemaphore`, and overlapped-event completion in `kernel32.cpp`.

A bounded-wake hypothesis applies only to auto-reset events and semaphore releases. Manual-reset events and thread completion must continue to wake all waiters. A naive wake-one policy is unsafe: if the only awakened waiter is a compound `WaitForMultipleObjects(..., all=true)` waiter that cannot yet complete, an eligible single-object waiter can remain asleep while an auto-reset event or semaphore remains signaled.

The exact `WaitKernel` source at `kernel32.cpp:220-245` was checked for timeout cleanup. Both finite and infinite waits execute the waiter-removal loop before returning. A preliminary timeout-leak suspicion was therefore disproved; no such bug or patch is claimed.

## Required experiment protocol

1. Compile baseline, naive bounded, and compound-guarded variants from exact preserved sources.
2. Test manual/auto events, semaphore counts, finite timeouts, single/any/all waits, mixed compound-all/single registration orders, and shutdown/progress behavior.
3. Reject any variant with lost progress, a race, a deadlock, or changed manual/sticky signaling semantics.
4. Measure notify/predicate work and end-to-end host wait throughput with interleaved repeated runs. Treat these as host synchronization results, never phone FPS or frame-pacing proof.
5. Inspect present statistics, ADPF, thread, Bink, and ordinary Play/diagnostic-tour interactions before integration. Device-dependent affinity or priority changes require device evidence and are not candidates here.

## Initial decision

No technique was adopted at the outset. The first priority was a correctness-first guarded notification model and a baseline-versus-guarded host benchmark. Physical-device frame times, thermal behavior, audio continuity, and sustained gameplay remain unmeasured.

## Incremental result — exact-header semantics model

At `2026-11-04T03:23Z`, four compiled variants completed the first model suite with zero expectation failures. This does **not** mean every policy is correct: the harness explicitly expects the unsafe variants to demonstrate lost progress.

| Policy | Mixed all=true event/semaphore | Pending multi-any event/semaphore | Disposition |
| --- | --- | --- | --- |
| Baseline broadcast | Progress | Progress | Reference |
| Naive bounded | **Stranded single waiter** | **Stranded single waiter** | Rejected |
| Guard all=true only | Progress | **Stranded single waiter** | Rejected |
| Guard every multi-object waiter | Progress | Progress | Candidate; timing/stress pending |

The suite also covered reverse registration order, manual/auto events, back-to-back semaphore releases, compound-all acquisition, and finite-timeout cleanup. Raw JSONL and exact candidate/header hashes are in the Phase 6 evidence directory. No production source or device-performance claim has changed.

## Incremental result — bounded notification rejected on timing

A refined candidate kept the original manual/thread `Notify()` unchanged and added a separate guarded `NotifyLimited` for auto/semaphore paths. A structure-only control isolated the added waiter field/assignment. Sixteen rotated fresh-process repetitions per policy/scenario produced 480 correct runs.

| Scenario | Multi-guard / baseline median | Multi-guard / structure median | Pair direction |
| --- | ---: | ---: | --- |
| Four-worker auto event | 2.231109 | 2.006187 | Slower 16/16 |
| Eight-worker auto event | 1.559205 | 1.544121 | Slower 16/16 |
| Four-worker semaphore | 1.746289 | 2.043458 | Slower 16/16 |
| Eight-worker semaphore | 1.453210 | 1.481128 | Slower 16/16 |

Four batch scenarios were neutral/noisy (0.986005–1.049498 versus baseline) and did not offset the single-release regressions. All runs met exact operation counts, zero timeouts, and final-state checks. **The complete bounded-notification line is rejected and no production edit is made.** Raw rows, summaries, fixture corrections, and hashes are preserved under `phase-06/notify-cohorts*`.

The next audit target is ordinary Present bookkeeping and existing frame-limit/ADPF behavior. Any new candidate must avoid the same mistake of optimizing a synthetic wake count while worsening end-to-end latency.

## Counted-event lead traced out of Android scope

`src/core/event.cpp` contains a compare-exchange-based counted-event read that initially looked like a scheduling candidate. Build tracing shows it belongs to the 32-bit Windows hook host through `src/host/Makefile`; it is absent from the Android `libmain.so` sources in `src/native/Makefile`, and the native registry has no corresponding replacement. No atomic-load experiment or production edit is justified as phone optimization. This negative build-scope result is recorded rather than spending a benchmark on an ineligible target.

## Present bookkeeping measured and retained

The actual tracker completed 550,872 correctly accounted synthetic Present intervals across five rates and 12 repetitions. Disabled-diagnostics ordinary calls had a 25 ns median at every rate. Window completion median/max measured 127.5/418 ns at 60 Hz and 10.604/21.023 µs at 10,000 Hz. Full per-call CSV, percentiles, first/later-window separation, and the corrected rounding setup failure are preserved. These small host costs do not justify changing the p99/sample or mutex implementation; no candidate was created.

## Target assembly and lazy-TID candidate rejected

Isolated Zig 0.17.0 generated actual `present_stats.cpp` and `kernel32.cpp` AArch64-musl assembly with Cortex-A76+RCpc flags. This exposed the ordinary-path TID guard as a measurable hypothesis, not an ARM timing result. An exact-adapter A/A/candidate experiment then ran 144 processes (24 rotated repetitions, off/on, 200,000 calls each).

The candidate's off/baseline ratio was 0.989265, but the A/A control/baseline ratio was 1.012942 with wide ranges. The median-total difference was only ~0.48 ns per call, and detailed mode was neutral at 1.003151. All Presented counts, detailed sample counts, and TIDs were correct. **The TID patch is rejected as noisy and immaterial; production remains unchanged.** Raw per-call arrays, process summaries, assembly, setup failures, and hashes are preserved.

## Stress, sanitizers, and regression coverage

- Mixed-waiter semantics ran under ASan+UBSan for all five policies and under TSan for baseline/multi-guard, with zero expectation failures or sanitizer reports.
- A further 144-process stress matrix passed: 64 mixed-waiter semantics runs and 80 corrected batch runs across 1–16 workers. Independent verification checked every progress expectation, operation count, timeout count, and final state.
- Twelve production regression commands initially passed in 9.185311 seconds and, after the adapter-test addition, passed again in 9.297053558 seconds: UBSan performance hints, CPU affinity, Present statistics, tour plan/guest time, diagnostics, runtime bridge, guest memory, diagnostic-tour runtime policy, performance policy/evidence, touch controls, and compatibility policy.
- `src/native/tests/present_stats_test.cpp` now also covers the global adapter's disabled sample/total gate and enabled TID/sample accounting. The enlarged test passes normally, ASan+UBSan, TSan, and against the rejected evidence-only TID candidate. A further 300 iterations passed (200 normal, 50 ASan+UBSan, 50 TSan). This is regression coverage, not a runtime optimization.
- Nine supplied binaries/logs/shader fixtures have hash-verified read-only status. No physical Play/tour, audio-device, shutdown-device, or frame-pacing run is available.

The final independent preflight passes all 55 non-time checks and refuses completion under 3,600 seconds (only the time gate failed at 3,430.938187 seconds). Ratio-tolerance and output-path wrapper failures are preserved and corrected; no result was changed. Exact-code CI and the actual elapsed-time gate are satisfied at completion; the post-commit finalizer and manifest are referenced below.

## Other scheduling/locking policies audited without a patch

- **ADPF:** the no-session atomic fast path already avoids its per-Present mutex. Active-session reporting remains serialized because the NDK session methods are not thread-safe. Skipping reports or changing target durations would alter clock policy without device evidence; rejected as a speculative optimization.
- **Affinity/priorities:** existing code reports the available CPU mask, does not force affinity, and maps engine priorities to nice values plus optional ADPF registration. Fixed affinity or new priorities are not justified by two-CPU host models.
- **Frame limiter/Present anchor:** limiter sleep remains on the engine loop while FPS/ADPF statistics count only successful Presents. Moving that anchor or skipping empty-loop limiter work would change gameplay/menu semantics rather than prove a pacing improvement.
- **Present/surface mutex:** it protects destroyed-surface and FIFO/Reset lifecycle behavior. No lock removal is justified without an Android surface-stress run.
- **D3D9 recursive lock:** potentially important translation overhead, but it belongs to Phase 8 and requires draw/state/lifetime profiling. A generic lock rewrite here would be unsafe and untargeted.
- **Bink timed locks/yields:** media-only polling and audio lead behavior belong to Phase 11. Replacing yields with sleeps or changing budgets could add A/V latency or underruns; no media timing change was made.
- **XAudio2 locks:** no audio-lock change without physical underrun/continuity evidence.
- **Handle lifetime:** the model does not exercise close-versus-wait or invalid-handle lifetime races. No new game trace establishes that scenario, and no lifetime semantics were changed. This remains a coverage limitation, not a claimed proof of safety.

The next evidence-based target is Phase 7 allocation/I/O: `WaitForMultipleObjects` allocates its object vector per call, and waiter vectors retain high-water capacity. Any allocation candidate must first measure real call sizes and preserve multi-wait and handle lifetimes.

## ADPF Present-thread attribution: inconclusive, no patch

A read-only historical-log audit found 61 detailed Present-outlier lines with 241 retained TID entries after excluding the diagnostics logger: 240 on TID 5192 and one on 5147. TID 5192 requested priority 0, which is not a priority-based ADPF registration path. This is a useful lead, not proof of a session-registration bug: the log contains no TID roster or thread-lifetime record, and its final ADPF window reports zero errors.

The pinned NDK-derived header (`P-404/android_frameworks_native` `c6a090fd350b6a8de7dc3ce51aaa1bc8d5493b9b`) describes a long-lived inter-related thread group and API-34 `setThreads` replacement semantics. It does not establish that the reporting caller must be a member. Explicit Present-thread registration is therefore **inconclusive/device-only**. No code was changed; a safe decision needs roster/lifetime evidence and sustained physical A/B pacing, not another host microbenchmark.

No wait-operation lines exist in the supplied logs, so the host wait scenarios also cannot be attributed to observed phone hotspots. Full historical-log counts, source lines, API provenance, and hashes are retained under `phase-06/`.

## Fidelity and release-preservation checks

An independent 31-check source-fidelity review passed for the wait model: preserved baseline hashes, fast paths, waiter registration, predicate waits, acquisition order, signal ordering, swap-removal cleanup, and baseline notification branches. Its intentional exclusions are explicit: fixed TID for object types that ignore it, no semaphore maximum field, no guest-handle/vector construction, and no Mutex/Thread/File/close-lifetime or Android scheduling coverage. This supports the notification comparison without overstating its scope.

The 0.15 `protodroid-v0.15` release remains downloadable with `prototype.apk` at 59,023,246 bytes; `main` remains `00976d9`, and no 0.16 release exists. Exact-code CI is required before the final gate; host and CI results remain distinct from physical Android evidence.

## Sustained baseline stress result

The corrected baseline-only run completed 16,000,000 operations in 430.408254992 seconds: four million each for four-worker event, four-worker semaphore, eight-worker event, and eight-worker semaphore. All exact operation counts, zero-timeout expectations, process exits, and final states independently verify. Descriptive throughputs were 32.088/33.109 µs per operation for the four-worker cases and 21.755/20.648 µs for eight workers; they are not candidate comparisons or device frame times. The initial missing-`bench` usage failure is preserved separately and excluded.

## Concurrent Present adapter stress

The actual process-wide adapter passed 40 fresh processes and 6,400,000 calls (20 normal, 10 ASan+UBSan, 10 TSan), with exact cumulative Presented counts, zero disabled-mode detailed samples, correct enabled totals/TIDs, empty sanitizer stderr, and independently verified summary sums. Of 3,200,000 detailed-mode calls, 3,102,383 positive intervals were retained; 97,617 nonpositive intervals were not recorded. The synthetic harness deliberately reads clocks concurrently before the adapter mutex, so out-of-order timestamps can produce that existing `duration_ns <= 0` rejection. Production D3D9 Present is serialized by `g_present_mutex`; this harness is a race/lifetime stress, not a device interval-loss or FPS measurement. No clock or locking change was made.

## TSan sustained stress

A second sustained cohort ran the baseline model under TSan for 1,600,000 operations in 79.358224174 seconds across the same four event/semaphore and four/eight-worker shapes. Every process returned zero with empty sanitizer stderr, exact operation counts, zero timeouts, and correct final state. These sanitizer timings are correctness evidence only and are never compared with normal-build throughput.

## Repeated full-regression stability

Twenty-four isolated repetitions of the complete 12-command focused regression passed all 288 commands in 207.031295757 seconds. Every iteration has a separate results/log directory, and every Present adapter log was independently checked for the new gate/TID coverage. This final stress does not overwrite the original final regression logs or change any source/result disposition.

## Final TSan adapter soak

A final 120.114001188-second TSan soak completed 458 fresh concurrent-adapter processes and 73,280,000 Presented calls with zero sanitizer stderr and exact per-process counts. Detailed mode retained 33,625,258 positive intervals; all raw rows and summary sums independently verify. This extends race/correctness coverage and is not a performance comparison.

## Phase-completion report

### 1. Actual time and substantive work

Phase 6 ran from `2026-11-04T03:19:00Z` to the completion reading at `2026-11-04T04:19:16.064254Z`: **3616.064254 seconds (1h00m16.064254)**. Work covered exact wait/notify semantics, unsafe and safe bounded-notification variants, A/A/structure-controlled host benchmarks, ordinary and detailed Present accounting, AArch64 structural inspection, TID attribution, ADPF policy/API review, sustained normal/TSan stress, concurrent adapter stress, and independent evidence validation. No idle time is claimed as engineering work; the minimum-time gate was enforced separately.

### 2. Research files updated

`README.md`, `phase-06.md`, `experiment-ledger.md`, `technique-matrix.md`, `profiling.md`, `rejected-changes.md`, `repository-ledger.md`, `device-results.md`, and `final-report.md` record the phase state and dispositions. Raw data, scripts, failures, hash manifests, and final verification remain outside Git under `/home/user/protodroid-016-evidence/phase-06/`.

### 3. Source revisions and ProtoDroid commits

The runtime baseline was merged `00976d939e0cba01af908a8aecd4a02db6d41066` (released 0.15 itself remains `18ba66bd0945e405aaded0de5ff8848a8cd07932`). Commit `f177ba0eb6f62be034e4c7d97327cfc1e8ff1076` added only the Present adapter regression and active research; `f9616456ea5edb2191581b2ecc5eb7505059fa02` preserved the negative scheduling research. The final documentation commit is the Git commit containing this report. Supplemental NDK-derived API inspection used `P-404/android_frameworks_native` `c6a090fd350b6a8de7dc3ce51aaa1bc8d5493b9b`, Apache-2.0 file notice, with no implementation import.

### 4. Techniques adopted, rejected, and inconclusive

- **Adopted runtime optimization:** none. The only C++ delta is test coverage for the existing global Present adapter.
- **Rejected:** naive and compound-all-only bounded wakes on lost progress; the multi-object-safe bounded-notification line on consistent single-release regressions; Present bookkeeping rewrite on immaterial measured cost; lazy ordinary-Present TID on A/A-scale noise and neutral detailed mode; counted-event work as Windows-hook-host-only; speculative affinity, ADPF-report, limiter-anchor, surface-lock, Bink-yield, and audio-lock changes without evidence.
- **Inconclusive/device-only:** explicit Present-thread ADPF registration. Historical TID attribution is not a session-roster, lifetime, or sustained-A/B proof.

### 5. Before/after measurements and raw data

- Bounded notification, 480 correct processes: multi-guard/baseline medians were **2.231109×** (four-worker event), **1.559205×** (eight-worker event), **1.746289×** (four-worker semaphore), and **1.453210×** (eight-worker semaphore), slower in 16/16 pairs for each. Raw: `notify-cohorts.jsonl` and `notify-cohorts-summary.json`.
- Ordinary Present bookkeeping, 550,872 correct calls: 25 ns ordinary median; window-finish median/max 127.5/418 ns at 60 Hz and 10.604/21.023 µs at 10,000 Hz. Raw per-call CSV: `present-stats-{rate}.csv`.
- Lazy TID, 144 processes/28.8 million calls: off candidate/baseline 0.989265, A/A control/baseline 1.012942, on candidate/baseline 1.003151; only ~0.48 ns/call median-total separation. Raw: `present-tid-cohorts.jsonl` and six per-call arrays.
- Sustained baseline: 16,000,000 operations in 430.408254992 seconds; TSan sustained: 1,600,000 in 79.358224174 seconds. Concurrent adapter: 40 processes/6,400,000 calls, with all 97,617 nonpositive synthetic intervals explicitly retained in the report as rejected samples, not hidden. A separate 120.114001188-second TSan soak completed 458 processes/73,280,000 calls with zero sanitizer output. Raw JSONL and independent verification files are alongside those runs.

These are host/model measurements, not physical frame times or FPS. No before/after device pacing delta exists.

### 6. Correctness and regression results

The final 12-command focused regression passed in 9.297053558 seconds, followed by 24 isolated full-suite repetitions with 288/288 commands passing in 207.031295757 seconds. Additional passes: 144-process mixed semantics/batch stress; 300 adapter regression iterations; 40-process concurrent adapter stress and 458-process TSan adapter soak; 16-million normal and 1.6-million TSan sustained operations; ASan+UBSan/TSan model suites; 31 model-fidelity checks; and 55 preflight non-time checks. Exact-code CI run `41768547555` completed successfully at `f177ba0`; later commits before this report changed documentation only. Read-only inputs, 0.15 release preservation, and all current hash manifests passed. `phase6-final-verification.json` and the final `phase6-manifest.json` provide the post-commit independent gate and file hashes.

### 7. Limitations and next priority

No physical Android device, ADB, GPU profiler, or Android/Bionic execution is available. Device FPS, p95/p99 frame times, audio continuity, shutdown, thermals, and sustained pacing remain unmeasured. The wait model excludes close-versus-wait lifetime, guest-handle/vector construction, Mutex/Thread/File waits, and real phone scheduling; historical logs are not exact-0.15/0.16 A/B data. No universal improvement or compatibility claim is made.

Proceed sequentially to **Phase 7: memory, allocation, RCF/asset I/O, and streaming**. First test two measured source-led hypotheses with A/A controls: defer basename construction on exact RCF archive hits, and avoid per-call heap storage for common small `WaitForMultipleObjects` counts. Preserve cold/warm separation, mod/loose-file priority, arbitrary handle counts, timeout/all/any semantics, and exact differential correctness; reject either change if gains remain within normal variance.

## Evidence continuity note

At the 2026-11-04T05:40 continuation boundary, the external evidence store was not carried into the fresh environment. Phase 6 raw files and the final manifest cited in this report are historical results from the completed run, not currently available local artifacts. The exact commits and CI run remain pushed/available; this note does not convert the earlier checks into new results or claim that the old raw store can now be independently reopened.
