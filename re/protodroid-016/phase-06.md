# Phase 6 — game loop, scheduling, locking, and CPU/GPU overlap

Status: **active**. Clean continuation started at **2026-11-04T03:19:00Z** from merged Phase 1–5 commit `00976d939e0cba01af908a8aecd4a02db6d41066` on `arena/5eb1c06c-protodroid`. No Phase 6 production change or performance result exists yet.

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

## Current decision

No technique is adopted yet. The immediate priority is a correctness-first guarded notification model and a baseline-versus-guarded host benchmark. Physical-device frame times, thermal behavior, audio continuity, and sustained gameplay remain unmeasured.

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

The actual tracker completed 550,872 correctly accounted synthetic Present intervals across five rates and 12 repetitions. Disabled-diagnostics ordinary calls had a 25 ns median at every rate. Window completion measured 127.5 ns median at 60 Hz and 10.604 µs median at 10,000 Hz, with a 21.023 µs maximum at the highest rate. Full per-call CSV, percentiles, first/later-window separation, and the corrected rounding setup failure are preserved. These small host costs do not justify changing the p99/sample or mutex implementation; no candidate was created.

## Target assembly and lazy-TID candidate rejected

Isolated Zig 0.17.0 generated actual `present_stats.cpp` and `kernel32.cpp` AArch64-musl assembly with Cortex-A76+RCpc flags. This exposed the ordinary-path TID guard as a measurable hypothesis, not an ARM timing result. An exact-adapter A/A/candidate experiment then ran 144 processes (24 rotated repetitions, off/on, 200,000 calls each).

The candidate's off/baseline ratio was 0.989265, but the A/A control/baseline ratio was 1.012942 with wide ranges. The median-total difference was only ~0.48 ns per call, and detailed mode was neutral at 1.003151. All Presented counts, detailed sample counts, and TIDs were correct. **The TID patch is rejected as noisy and immaterial; production remains unchanged.** Raw per-call arrays, process summaries, assembly, setup failures, and hashes are preserved.

## Stress, sanitizers, and regression coverage

- Mixed-waiter semantics ran under ASan+UBSan for all five policies and under TSan for baseline/multi-guard, with zero expectation failures or sanitizer reports.
- A further 144-process stress matrix passed: 64 mixed-waiter semantics runs and 80 corrected batch runs across 1–16 workers. Independent verification checked every progress expectation, operation count, timeout count, and final state.
- Twelve production regression commands passed in 9.185311 seconds: UBSan performance hints, CPU affinity, Present statistics, tour plan/guest time, diagnostics, runtime bridge, guest memory, diagnostic-tour runtime policy, performance policy/evidence, touch controls, and compatibility policy.
- `src/native/tests/present_stats_test.cpp` now also covers the global adapter's disabled sample/total gate and enabled TID/sample accounting. The enlarged test passes normally, ASan+UBSan, TSan, and against the rejected evidence-only TID candidate. This is regression coverage, not a runtime optimization.
- Nine supplied binaries/logs/shader fixtures have hash-verified read-only status. No physical Play/tour, audio-device, shutdown-device, or frame-pacing run is available.

The 43-check independent preflight passes every non-time check and refuses completion under 3,600 seconds. The first ratio-tolerance and output-path wrapper failures are preserved and corrected; no result was changed. Phase 6 remains active pending remaining substantive review and the actual-time gate.
