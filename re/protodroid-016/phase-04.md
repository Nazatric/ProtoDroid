# Phase 4 active checkpoint — recompiler, dispatch, and ARM64

Status: **active; not complete**. Exhaustive host layout testing found workload-dependent tradeoffs rather than a safe universal win. The provisional split-layout patch was **reverted**; no runtime optimization remains in the working tree. A permanent collision regression is retained. No physical-device FPS, frame-pacing, Android-timing, or release claim exists.

## 1. Actual time and work

- Started: `2026-11-03T20:28:45Z`, from pushed Phase 3 commit `f1737b23a6c4fcaf9255b0f4f4cd765ea2f5a704`.
- Latest checkpoint: `2026-11-03T21:08:43Z` (about 40 minutes of active work; the one-hour minimum has not been reached).
- Work so far: extracted the complete function table; built digest-checked dispatch fixtures; measured original, split, parallel-index, 17/19-bit, static-BSS, packed-slot, interleaved-index, and aligned-bucket layouts; ran standard and two independent locality protocols; applied then reverted split after localized regressions; added permanent collision coverage; audited all generated source and one AArch64 generated partition; and independently verified 2,778 timed raw processes.
- Phase 1–3 raw artifacts remain historical committed records after the external-store loss. Phase 4 raw artifacts are under `/home/user/protodroid-016-evidence/phase-04/`; recovery details are in its `RECOVERY.md` and `/home/user/protodroid-016-recovery/2026-11-03T2027Z`.

## 2. Exact source, environment, and workload

- Baseline and current restored source: `src/native/runtime.cpp` SHA-256 `f19dce078f4ee54f2c1cb831df47335474e64c44933edf6b51d53ebef2faadf1`, matching `f1737b23`.
- Measured split source: original evidence variant SHA-256 `fface55e1136d624e1eae5fa8a82f16492f82f22e612c1fb8c6c0a1b7830f5ae`; the temporarily integrated comment-only variant had SHA-256 `e4c117dc40c369e05b99071a85cdbd7a37b438303b59ad23cfe008e5dc540299`. Neither remains in production.
- Input: all **126,974** unique address/function associations from `build/recomp/native/table.c`, with actual `runtime.cpp`/`imports.cpp` and checked generated stubs. Address-list SHA-256 `515b2e2a1942023cd73fa0bf026efd13eb005b6acd41f442baf6b9b3a416f4d8`; table SHA-256 `a8a9db5879470b58a35b43bbd84cb8c0e40ff1c68672edb114b2804b90491382`; DLL SHA-256 `d331be205bdde99897952ee88261ab7e63c5c65e36a4660e2a367886a3537071`.
- Host: x86-64 Linux, GCC 12.2.0, two logical CPUs; normal `-O2`, separate UBSan, ASan+UBSan, and TSan builds. No game scene or rendering cache was executed.
- Cold: fresh process/first hash construction, no OS-cache-dropping claim. Warm: full-table verification before seven timed batches. Standard modes are sequential hits, one repeated target, guaranteed misses, and full dispatch. Locality modes use 16/256/4,096/16,384 unique targets, mixed hits/misses, and localized dispatch.
- Initial locality uses evenly spaced unique real entries. Independent confirmation uses uniform partial-Fisher-Yates sampling without replacement and shuffled nested hot sets. These are sensitivity tests, not an actual target trace.
- Direct-shell launches are required for cold RSS. Python-launched cold RSS inherited the parent's 16,204-KiB high-water floor and is explicitly invalid.
- Structural target: isolated Zig 0.17.0/LLVM 22.1.8, `aarch64-linux-musl`, Cortex-A76+RCpc, production-like `-O2 -g0 -fPIC`, flat guest base `0x1000000000`, TSO. This is not Android/Bionic, NDK release-binary equivalence, execution, or timing.
- Commands, source/binary hashes, generators, raw per-process logs, preserved failures, and summaries are under `phase-04/dispatch/` and `phase-04/generated/`.

## 3. Original/split/index standard cohorts

The original slot contains a 32-bit key plus padding and a 64-bit function pointer (16 bytes on x86-64). Split uses parallel key/function arrays (3 MiB total). Parallel indexed slots store key/index arrays and fetch the function through the immutable generated table.

Thirty cold processes/layout and 24 order-balanced warm processes/layout/mode, 2,000,000 calls × seven batches:

| Metric | Original | Split | Parallel index |
| --- | ---: | ---: | ---: |
| Cold first lookup median | 2.315824 ms | 1.778324 ms | 1.470279 ms |
| Cold process maxRSS median | 11,668 KiB | 10,472 KiB | 9,556 KiB |
| Sequential lookup median | 14.150570 ns/call | 12.310487 | 12.873943 |
| Repeated-target median | 2.662915 ns/call | 2.444967 | 2.792946 |
| Known-miss median | 25.983409 ns/call | 15.728258 | 21.021892 |
| Full dispatch median | 21.598687 ns/call | 20.865097 | 18.789381 |

A separate 36-process/layout/mode confirmation used 5,000,000 calls × seven batches:

| Mode | Original ns/call | Split ns/call | Paired split/original median ratio |
| --- | ---: | ---: | ---: |
| Sequential lookup | 14.193864 | 12.466144 | 0.875280 |
| Repeated target | 2.665343 | 2.459777 | 0.919683 |
| Known miss | 25.560324 | 15.328398 | 0.601714 |
| Full dispatch | 22.684933 | 21.398173 | 0.937314 |

Full-dispatch paired p95 was 1.002434. These standard results justified **provisional** integration only; they were not sufficient for final adoption. Raw data: `dispatch/ab-summary.*`, `confirm-summary.*`, and per-process logs.

## 4. Locality results and final split disposition

Two independent protocols exposed regressions that the standard cohorts did not cover.

| Case | Evenly spaced split/original paired median | Shuffled split/original paired median |
| --- | ---: | ---: |
| Lookup, 16 targets | 1.048833 | 1.035959 |
| Lookup, 256 targets | 1.036827 | 1.034367 |
| Lookup, 4,096 targets | 0.931069 | 0.959913 |
| Lookup, 16,384 targets | 1.120699 | 1.157686 |
| Mixed, 16 targets | 1.022126 | 1.017227 |
| Mixed, 4,096 targets | 0.982776 | 1.010203 |
| Dispatch, 16 targets | 0.992150 | 1.011768 |
| Dispatch, 4,096 targets | 0.976991 | 0.936729 |

The shuffled protocol used 12 alternating pairs/case and 5,000,000 calls × seven batches. Later three/four-layout screenings reproduced the same directional small-set and 16,384-target lookup costs, with run-to-run variability retained in raw summaries.

**Final decision: reject split for 0.16 production at this evidence gate and restore the original runtime.** A smaller cold/table footprint and large known-miss gain do not establish a beneficial real target mix, while localized raw lookup regresses up to about 16% in the independent confirmation. Full dispatch sometimes improves, but 16-target dispatch is flat/regressing and there is no game trace or Android result to weight the modes. Parallel indexed slots were already rejected for repeated-target regression and are not revived.

Raw data: `dispatch/locality/`, `locality-confirm/`, `index8/`, `packed12/`, and `buckets/`.

## 5. Other layout experiments

- **Flat direct table — rejected analytically.** Real entries span `0x7c001005`–`0x7cd8ba20`, lack uniform alignment above one byte, and would need 113,594,592 pointer bytes at byte granularity versus 3,145,728 split-hash bytes. The initial two-column token parse was invalid and preserved; corrected analysis uses exactly 126,974 first-column addresses.
- **17-bit table — rejected.** Load factor 0.968735 gives mean 12.873683 successful and 446.796510 synthetic-miss probes, with maxima 3,601/3,648. Only a separate bounded smoke followed correctness checks; it is not pooled with standard timing and its Python RSS is invalid.
- **19-bit table — not adopted.** Direct-shell cold median 3.212444 versus 2.139620 ms for 18-bit (paired ratio 1.525857), maxRSS 13,672 versus 10,476 KiB. Warm 19/18 ratios are 0.931419 lookup, 1.010304 repeat, 0.753250 miss, and 0.966940 dispatch. The memory/cold/repeat trade lacks a real-workload justification.
- **Static-BSS 18-bit table — inconclusive/not adopted.** Static/heap cold paired median 0.982646 with p95 1.058392; warm ratios 0.980445 lookup, 1.017245 repeat, 0.970544 miss, and 0.976700 dispatch. It moves 3,145,760 bytes into host ELF NOBITS, with unknown Android loader behavior.
- **Alternative multiplicative constants — no candidate promoted.** Eight constants across successful insertion and four guaranteed-outside miss sequences produced small workload-dependent tradeoffs, not a robust winner. See `dispatch/hash-function-model.json`.
- **Interleaved 8-byte key/index — rejected.** Although standard full dispatch improved in one screening, 16,384-target lookup and 4,096-target dispatch paired ratios reached 1.808098 and 1.169088 because the generated-table function fetch adds work/cache pressure.
- **Packed 12-byte key/function slots — rejected.** Unaligned-pointer packing did not provide a robust win: sequential lookup paired ratio 1.004018, 16-target lookup 1.027609, 16,384-target lookup 1.031651, and 4,096-target dispatch 1.031758. No unaligned-pointer production change is justified.
- **Aligned two/four-entry buckets — rejected.** They avoid distant split arrays but add indexing overhead and remain workload-dependent. Bucket2/bucket4 repeat ratios were 1.0676/1.0996; 16,384-target lookup 1.0705/1.0521; neither improves the standard miss path. The layout search stops here rather than overfitting more variants.

## 6. Correctness and generated-code evidence

- All final measured layouts passed the stated full association/dispatch/miss checks and per-timed-group digests. Original, split, and parallel index passed normal/UBSan/TSan and eight-thread first-use. Original and temporarily integrated split also passed ASan+UBSan full verification/concurrency.
- The temporarily integrated split passed runtime bridge, guest memory, 12 detour checks, 20 plugin ABI v1/v2 checks, and feature policy. The restored original passes the enlarged runtime bridge and ASan+UBSan full-table verification. No failure is converted into a pass.
- `src/native/tests/runtime_bridge_test.cpp` retains five same-hash entries with distinct results, a same-hash unknown target, and zero-key coverage. It passes against both original and split runtimes under UBSan.
- Production split AArch64-musl `RecompLookup` matched the measured split candidate's normalized 43-instruction body, retaining `LDAPR` and `__call_once`. This verified the temporary integration source, not a currently applied patch or Android performance.
- Generated-source census covers all 192 files/126,974 functions: 280,274 flat-direct guards have their fallback dispatch eliminated, leaving 23,575 residual sites (1,671 constant and 21,904 computed). Source contains 579,100 shared scalar and 301,179 plain-stack scalar access sites. Counts are static, not hotness.
- Generated `part_0005.c` AArch64-musl inspection has exactly five source residual dispatch sites and five assembly call/branch sites, no `RecompDirect`, and static 159 acquire/142 release/272 DMB instructions. Slow-path/static instructions are not dynamic costs. **No TSO or unaligned-access relaxation was adopted.**
- Full native linking is unavailable because DXVK/FAudio/FFmpeg dependency directories are absent. No full-host or APK build is claimed.

## 7. Independent evidence verification and failures

`phase-04/verify_phase04.py` independently parsed **2,778 timed processes** and passed:

- 90 initial cold, 288 initial warm, and 288 focused-confirmation processes;
- 1,824 locality/alternative-layout processes;
- 288 size/static cold/warm processes;
- exact entry/iteration/batch counts, per-repetition digest agreement, count digests, and independent recomputation of every locality/alternative median and paired-ratio summary;
- restored production runtime SHA-256 `f19dce078f4ee54f2c1cb831df47335474e64c44933edf6b51d53ebef2faadf1`.

Preserved failures include the initial parser/link failures, parallel-index target-zero bug, TSan warning-promotion setup, ARM target/label/parser failures, incomplete production conversion, wrong plugin fixture path, invalid Python RSS, locality generator marker, bucket allocation cast, and assembly extraction attempts. They are diagnostic/setup history, not hidden production results.

## 8. Limitations and next priority

- Exact addresses with synthetic sequences/stubs do not establish actual target frequencies, Android cache behavior, Bionic `call_once`, device memory, FPS, frame-time distribution, pacing, thermals, or sustained smoothness.
- Instruction counts are not cycles. AArch64-musl output is not Android/Bionic or release-binary proof.
- Host maxRSS is not Android memory accounting. Invalid Python-launcher RSS is excluded.
- No graphics quality/features, controls, mods, saves, Bink, FIFO/Reset, AA, drivers, launcher, or TSO semantics changed.

Next: finish the final Phase 4 source/regression/evidence checks only after at least one hour of substantive work. With the layout search exhausted and no safe TSO relaxation supported, the expected phase outcome is a **negative dispatch-optimization result plus permanent regression coverage**, not a forced patch. Phase 5 native hot paths remains sequential and has not started.
