# Skate 3 comparison and ProtoDroid performance investigation

Date: 2026-11-05. This is a post-Phase-14 investigation, not a new phase-completion or physical-device-validation claim.

## Scope and exact inputs

The question is where ProtoDroid can obtain a substantial quality-preserving improvement now, and why Buku313's Skate 3 port can report much higher performance. The answer below separates measured evidence, source-provable architectural differences, and candidates that still require an A/B experiment.

| Input | Exact identity | Use and boundary |
| --- | --- | --- |
| ProtoDroid working source | `0d86686952f9eefdcc27f8446678ab2b11b3719e` | Current 0.16/code-16 PR source; not the source that produced the capture |
| Captured ProtoDroid runtime | `61869cbb9662519be128234111b51d9ee6d37ec0` | Source revision printed by the supplied native log |
| Supplied log tree | `origin/Logs` at `ba0e648ea7a510006ef4f554d48aa9853ce6e1cb` | Read-only extraction under `/home/user/protodroid-016-evidence/continuation/device-logs-ba0e648e/` |
| Skate3-Mobile | `e1b28c185d578c22e3a630f6f3b13ed123462a43` | Real renderer, profiles, documentation, and release evidence |
| RexGlue SDK selected by that Skate 3 revision | `edd4344723ecac3ffa18c5dcd2fcc268f468ff9e`, branch `android-arm64` | Real portable-C++ code generator and memory/synchronization templates |
| DXVK baseline used for source comparison | v2.5.3 `c707d902…` with all 16 ProtoDroid Android patches | Existing pipeline/cache, draw, state, lock, and query behavior |

No Android device, `adb`, AGI, Mali profiler, Android NDK build, or new APK/device run was available in this environment. The supplied capture is evidence from its recorded runtime only. It is not relabeled as a result for the current 0.16 candidate. Reproducible local analysis outputs are `analysis.json`, `analysis.md`, `pipeline-analysis.json`, and their analyzer/stdout/hash companions in the external evidence directory.

## Executive answer

There is no supported single switch that makes ProtoDroid match Skate 3. The highest-value near-term work is to split two different problems:

1. **Cold/first-encounter stalls:** preserve real shader and pipeline state, but investigate moving already-known warm-cache pipeline completion into a bounded real loading interval. Also break down the multi-second `Present` and single large `LockRect` observations before changing synchronization or copying. This targets gameplay smoothness; it cannot make cold driver compilation free.
2. **Ordinary-play frame cost:** obtain a matched warm, ordinary-play baseline with heavy tracing and import profiling off, plus real GPU timing and DXVK-internal draw/state/query attribution. If the protected D3D9 submission path dominates, investigate batching and redundant-work elimination with semantic evidence. If the GPU dominates, the substantial quality-preserving work is rendering culling/pass/resource optimization, not another micro-optimization in the import layer.

Skate 3 is fast for reasons that are not equivalent to a faster version of ProtoDroid's current renderer. It reconstructs a game-specific native Vulkan scene renderer, culls and caches game scene objects, uses offline-compiled shaders and loading-screen pipeline construction, and offers profiles that reduce resolution, world range, materials, grass, and post effects. ProtoDroid instead translates general PowerPC code and the game's D3D9 workload through DXVK. Skate 3's 512x288 Performance image and ProtoDroid's captured 2400x1080 image are not the same workload; 1280x720 Quality is also only about 35.6% of ProtoDroid's captured pixel count.

## What the new ProtoDroid capture actually shows

### Capture conditions

The capture is a Redmi `2412DPC0AI`, MT6899, Mali-G720 MC7, Android 16, 120 Hz device. It renders at **2400x1080**, with quality/shadow detail 2, anisotropy 2, BC decoding on, and FIFO presentation. It uses the isolated `manhattanTest` diagnostic tour with **4x guest clocks, import profiling, pipeline timing, and heavy pipeline tracing**. DXVK reports no state-cache file in this isolated tour. Visual correctness was not verified by the log analysis.

Consequently, the capture is valuable for finding stalls and hot paths, but it is not an ordinary-Play, warm-cache, visually validated performance benchmark. The heavy pipeline trace logs before each driver call and can itself be expensive. The 4x guest-clock mode also changes the relationship between guest work and real-time presentation.

### Whole-run facts

- **11,027 presents**, **21,023 gameplay dispatches**, **174.555 s** wall time, and **500.684 s** process CPU time (**286.83%** of one core).
- **11,026** native present intervals; logged average **61.313 FPS / 16.310 ms**, worst interval **3319.335 ms**.
- Counts above 8.5/17/34/50/100/250/1000 ms: **10462 / 3060 / 142 / 69 / 32 / 5 / 3**.
- The average hides the important distribution: there are both many intervals above 17 ms and three multi-second stalls.
- Across 179 disjoint native windows, descriptive totals include 25,361 DXT calls/826.2 ms, 1,332 reads/1830.15 ms, and 11,027 Present calls/about 11,593.26 ms of inclusive Present wall time. These categories overlap and are not additive exclusive frame-time attribution. Present wall includes FIFO/vsync and is not GPU time.
- Final thread CPU leaders are the render thread (109.64 s), main thread (107.81 s), and `dxvk-cs` (52.92 s). Six game workers each accumulated roughly 21.6-22.84 s. Cumulative thread CPU does not identify which thread limits an individual frame.

### The three multi-second stalls are not one mechanism

The worst interval, **3319.335 ms**, coincides with a 3308.395 ms `EngineService` sample, while that frame's own Present wall sample is only **0.020 ms**. A following 1489.384 ms interval has 1492.071 ms `EngineService`, but the largest Present wall sample in that one-second bucket is 199.304 ms. These associations do not prove that either stall is pipeline compilation or Present itself.

The third multi-second interval is a useful counterexample to dismissing Present: the **3219.508 ms** interval at `mono=21937.806` has a **3188.963 ms** Present wall sample. The surrounding pipeline snapshots contain new graphics-pipeline work, and both `dxvk-cs` and `mali-compiler` are active. That is consistent with first-encounter pipeline/driver work contributing to this stall, but the capture has no begin/end compilation record or GPU timestamp that proves the full causal chain.

### Pipeline census, corrected for instrumentation limits

- Twenty-two lightweight timing snapshots represent **331 graphics + 8 compute** driver calls, with zero logged errors. Their weighted logged driver times are **9402.944 ms + 40.649 ms**, averaging **28.408/5.081 ms**, with maxima **58.906/9.885 ms**.
- The lightweight logger resets counters approximately every five seconds and does not flush residual counters. Those totals are therefore incomplete.
- The separate heavy trace contains **341 attempted graphics calls**, 682 stage records, 134 unique vertex-shader hashes, 165 unique fragment-shader hashes, 340 usable VS/FS pairs, and 179 unique pairs.
- Conservative canonicalization yields 329 descriptions and 12 duplicate-like extra calls. Specialization values, layout definitions, output handles, return results, and object lifetimes are absent. The 12 are **not** proof of 12 safe deduplication opportunities, and the 341 begin records are not proof of 341 successful compilations.
- Heavy tracing, shader dumping, and `fsync` are outside the reported driver timer. `dxvk-cs` CPU time cannot be substituted for driver compilation time.

This supports continued work on cold-stall scheduling and cache behavior. It does not support resurrecting textual pipeline deduplication or claiming a fixed number of milliseconds can be removed.

### `LockRect` is a lead, not a root cause

The render import window ending at `mono=21896.142` contains one `IDirect3DTexture9::LockRect` observation at a rounded **14%** of that ten-second inclusive profile window. Nearby DXT/create/upload counters do not explain that time.

The bridge's inclusive call can contain the host DXVK lock, allocation of a guest shadow, a copy into that shadow, unlock/copy-back behavior, and texture adaptation. With texture mods disabled in this capture, adaptation is not a sufficient explanation by itself. A host lock can also synchronize with outstanding GPU work. The current record cannot distinguish host synchronization from bridge copying, and the rounded percentage is not an exact call duration. Instrument host-lock and shadow/adaptation subphases before changing either.

### The steady/late-tour evidence rejects a D3D9-only theory

Late diagnostic-tour render windows are often guest/sleep/wait-heavy, with Draw around 2% in the final sample; recent main windows show guest 56-64% and waits roughly 27-36%. This is not a consistently D3D9-import-saturated run. It also is not a GPU-bound proof: there is no GPU frame-time series, and inclusive guest/wait time can conceal the producer/consumer dependency that actually limits a frame.

The import census nevertheless identifies paths worth measuring inside DXVK. Observed render-thread call minima include:

| Call | Observed minimum |
| --- | ---: |
| `memcpy` | 40,593,593 |
| `DrawIndexedPrimitive` | 11,766,054 |
| `SetStreamSource` | 11,677,085 |
| `SetIndices` | 11,361,687 |
| `SetTexture` | 4,740,024 |
| Query `GetData` | 1,163,320 |
| Vertex-buffer `Lock` | 798,207 |

Only each window's top ten imports are logged, so these are lower bounds rather than complete call totals. They do not show how many calls changed state. DXVK already early-outs unchanged stream tuples, index pointers, and texture pointers. Generated ProtoDroid forwarding thunks do not take a separate blanket bridge lock. Device creation logs `behavior 0x44`, which includes `D3DCREATE_MULTITHREADED`, so DXVK's protected-device path and its device lock are relevant to future internal timing.

No query-type breakdown, copy-size distribution, changed-state count, internal draw cost, or GPU frame-time series exists. Therefore no safe claim yet follows for query removal, small-copy specialization, global state deduplication, MultiDraw, or lock removal.

## Why Skate 3 performs well

### 1. It replaces much more than CPU instruction translation

Both projects statically recompile PowerPC game logic, but that is where the similarity ends. RexGlue generates portable C++ ahead of time. Skate 3 then reconstructs rendering with a **game-specific native Vulkan scene renderer** (`src/skate3_native_scene_gpu.cpp`). It builds native scene/item caches, culls objects, prepares meshes/textures, and constructs its own Vulkan pipeline families. ProtoDroid must preserve the semantics of a general D3D9 command stream through DXVK, including dynamic state, resources, locks, queries, resets, and presentation.

A different CPU code generator alone cannot reproduce that rendering advantage. Replacing ProtoDroid's recompiler with RexGlue, changing C++ dialect, or adopting another host runtime would leave the D3D9 workload intact.

### 2. It knows its shaders and pipelines ahead of time

Skate 3 has offline shader artifacts, loading/mesh prewarming, and explicit native pipeline construction. Its loading path deliberately builds real pipeline families behind a real world loading screen and accepts longer loading rather than presenting that compilation as free. This is a useful scheduling idea.

It is not a universal cold-cache solution. Skate 3's native renderer has a bounded, game-specific pipeline family; ProtoDroid must retain the actual shader bytes, specialization, layouts, state, and lifetimes of the original workload. Its source also documents asynchronous-compilation paths that can skip draws while a pipeline is unavailable, with special handling for one-shot menu renders. ProtoDroid must not copy that completeness tradeoff or count skipped rendering as an engine-FPS improvement.

### 3. Its performance profiles are not quality-equivalent

At the pinned revision, the documented profiles include:

- **Performance:** 512x288, reduced world/LOD range, simplified materials, and disabled grass/expensive post effects.
- **Quality:** 1280x720 with more rendering features and greater range.

Pixel-count normalization alone gives:

| Comparison | Pixel ratio |
| --- | ---: |
| ProtoDroid 2400x1080 / Skate 3 Quality 1280x720 | **2.8125x** |
| ProtoDroid 2400x1080 / Skate 3 Performance 512x288 | **17.578125x** |

That is not a complete GPU-cost model, but it decisively prevents comparing headline FPS as though the render workloads were equal. The pinned Skate 3 README itself describes 60 FPS as a target and guest cap, not a performance guarantee, and still warns about visual bugs, missing effects, and device-specific differences. Resolution reduction, shorter draw distance, disabled effects, upscaling, or frame generation may be legitimate user-facing quality/performance modes only when explicitly labeled; they are not evidence of a quality-preserving engine optimization.

### 4. Memory-ordering differences are architectural, not a measured FPS explanation

RexGlue's ordinary PPC memory macros use direct volatile accesses and its synchronization builders emit explicit fences. ProtoDroid maintains x86-like TSO for guest PPC shared memory on ARM64 with acquire/release accesses, while private-stack accesses remain plain. This can have a different cost profile, but no matched function-level profile measures its contribution here.

Do not globally disable TSO or weaken ordering based on this comparison. A sound proof of private/ single-threaded storage and physical-device correctness/performance validation would be required for any narrower candidate. This is not the first near-term optimization.

## Ranked work and decision gates

### Priority 0: produce a usable ordinary-play baseline

This is measurement work, not an FPS change.

1. Build and test one exact source revision for both arms; do not compare a merge/PR revision log with a different source build.
2. Use ordinary Play, not the diagnostic tour; same level/route, resolution, quality, shadows, anisotropy, FIFO, audio, drivers, and device thermal starting conditions.
3. For steady runs, disable heavy pipeline tracing and import profiling, restore normal guest clocks, and use a genuinely warm cache. Keep lightweight raw interval/thread diagnostics. Separate a deliberately cold/traced run from the steady baseline.
4. Record raw per-frame intervals and report median/p95/p99/max, threshold counts, process/per-thread CPU, load-to-interactive time, and thermal behavior over repeated matched runs. Verify rendered frames visually.
5. Add supported GPU timestamps or a validated device profiler. Do not infer GPU time from Present, `dxvk-cs`, or driver compilation counters.

### Priority 1: cold-stall scheduling and exact outlier attribution

- **Conditional candidate:** expose a bounded DXVK warm-cache pipeline-drain operation and evaluate it only at a real ProtoDroid loading boundary. Use actual cached shader/state entries; do not invent variants, create a second cache, hold the device/Mali lock while waiting, or wait indefinitely. Compare both loading time and in-game stall percentiles. The expected best case is fewer warm-run gameplay stalls, not less total first-run driver work. This is distinct from the previously rejected speculative whole-cache startup reconstruction and remains unadopted pending queue/loading-boundary evidence.
- Add begin/end/result and queue-wait timing around actual pipeline creation so attempted calls, successful compilation, background work, and blocking use can be distinguished.
- Split `LockRect` into host-lock, allocation, copy-in, adaptation, unlock/copy-back, and upload subphases. Correlate individual multi-second Present samples with pipeline, query, resource, and GPU activity.
- Do not call this complete until cold and warm runs both have visual completeness checks and matched loading/gameplay measurements.

### Priority 2: find the real ordinary-play submission cost

Instrument DXVK internally on the actual protected-device path:

- device-lock wait/hold time;
- draw validation, state lookup, shader/pipeline lookup, command recording, and flush time;
- unchanged versus changed stream/index/texture/render-state updates;
- query type, result wait, and polling cost;
- resource-lock and upload/readback cost;
- GPU time by pass/draw where supported.

Only then choose the implementation. If changed-state submission and draw recording dominate, a semantics-preserving batching API or game-renderer command reduction may be material. If most setters are unchanged and already cheap, broad deduplication will not be. If query waits dominate, fix the specific query pattern rather than removing queries. If `memcpy` matters, obtain size/alignment/caller distributions before considering specialization; the current handler already reaches host `memmove` and no size evidence exists.

### Priority 3: larger quality-preserving renderer work

A Skate-3-like native scene renderer is an architectural project, not a patch. It could avoid general D3D9 translation and enable game-aware culling and resource preparation, but requires reconstructing Prototype's scene, materials, shadows, post effects, Bink/UI interactions, mods, dynamic shadows, resets, and texture workflows. It must not be started under the assumption that Skate 3 code can simply be dropped in.

Smaller renderer optimizations remain possible if GPU/profiling evidence identifies them: better culling, fewer truly redundant passes/draws, or lower resource/upload cost while preserving the image. The current capture does not establish which one is profitable.

### Recompiler work only after function-level attribution

ProtoDroid already has whole-function flag liveness/dead-flag elimination, and flat-memory direct calls already avoid the old runtime range restriction. Those are not new opportunities. Use guest function/PC profiling to find actual hot recompiled functions before changing codegen. Host import percentages are not a substitute for that profile.

## Non-actions for this investigation

No production performance optimization is adopted from this evidence alone; the Phase 15 slice below is diagnostic instrumentation. In particular, do not:

- equate Skate 3's reduced-quality Performance mode with a ProtoDroid engine-FPS gain;
- replace RexGlue/Recomp, SDL, Android-patched DXVK, or the whole runtime based on headline FPS;
- globally relax guest memory ordering;
- resurrect textual pipeline deduplication or claim 12 removable pipelines;
- remove queries, locks, state validation, synchronization, effects, or draw submission without semantic and visual proof;
- skip frames/draws while shaders compile and count that as complete rendering;
- use cosmetic FPS, frame generation, upscaling, unconditional cutscene skipping, or a hidden resolution/effect reduction as an engine-performance result;
- treat the diagnostic-tour 61.313 FPS average or late-tour descriptive cohorts as ordinary gameplay performance.

## Phase 15 implementation slice: outcome and protected-path attribution

The first source implementation is **measurement-only**; it does not claim an FPS improvement or change rendering features:

- Native rect-only diagnostics split host `LockRect`, shadow-mutex wait, allocation, copy-in, adaptation, copy-back, and scratch-free phases. Host `UnlockRect` remains in the existing texture-upload timer. Buffer and box locks do not feed these rect counters.
- DXVK pipeline diagnostics now distinguish begun/completed/succeeded/failed calls, compilation-required outcomes, in-flight calls, wait/trace/driver phases, and a final residual snapshot. Attempted trace records are no longer the only outcome evidence.
- An independently opt-in `PROTODROID_D3D9_TIMING` path instruments the actual protected indexed-draw, stream-source, indices, texture-state, and query-`GetData` methods. Seventeen bounded aggregate operations include lock wait, inclusive method/upload/prepare/emit phases, query type/result/flush flags, and state changed/StateBlock-recorded counts. `Present` performs periodic logging after the swapchain call; ordinary device destruction drains residuals after device idle.
- State `changed` means DXVK's actual `needsUpdate` or pointer-change result, not merely a different incoming call. State `flagged` means StateBlock recording; query `flagged` means `D3DGETDATA_FLUSH`. Result fields on pure lock/upload/prepare/emit phases are not HRESULT evidence. Nested inclusive times are non-additive, relaxed residual snapshots are approximate, and none of these values is GPU time.
- Ordinary Play defaults this higher-overhead method timing off and clears stale tour environment. The diagnostic tour enables it deliberately; it must not be used as a steady FPS baseline.
- A separate opt-in/tour raw Present-interval CSV persists the samples already collected for detailed percentiles. The diagnostics worker writes sequence number, monotonic Present-end timestamp, and exact nanosecond interval before percentile sorting; there is no new Present-path file I/O. Files rotate one generation, retain a valid row prefix on failure, and stop at 64 MiB. `tools/analyze_frame_intervals.py` computes exact global nearest-rank p50/p95/p99 and threshold counts, rejects sequence/timing discontinuities, hashes the input, and labels unclosed prefixes rather than calling them complete runs.

Validation on the complete 18-patch series from exact DXVK `c707d9026f33b6ab89639f154b6ac5f6326fa037` includes clean application and byte comparison, 56 pipeline-outcome structural checks with four detected mutations, 81 submission-timing structural checks with six detected mutations, enabled/disabled UBSan runtime tests including 4,000 concurrent records and residual reset, and strict syntax compilation of Android and non-Android device/query translation units with the pinned headers. Existing DXVK present/frame-pacing/preflight/BC/unified-memory/depth-resolve/FXAA tests and native diagnostics/rect/tour/runtime/evidence tests also pass. The raw writer passes UBSan file/rotation/residual/cap tests and 25 structural checks with five detected mutations; the analyzer passes exact-statistic/hash tests and rejects eight malformed-trace classes. These are host/source gates, not Android rendering or phone-FPS proof.

A follow-up patch 19 adds only a **dormant bounded warm-cache drain primitive**. It counts queued plus active state-cache feeder work and saturated pipeline-worker pending work, polls outside device/worker locks with a one-millisecond interval, and enforces a 30-second deadline. It creates no shaders/pipelines and has no D3D9, Present, destruction, or other production caller because a whole-loading-screen boundary remains unproven. Fresh 19-patch application and seven-file byte comparison pass, along with 26 structural checks/six mutations, fake-clock UBSan tests (including the feeder-active race, partial poll, timeout, and cap), six strict translation-unit variants, and the existing state-cache suite.

Read-only disassembly of the supplied `prototypeenginef.dll` sharpened that boundary disposition. `streampackagestates::Loading` has a three-function vtable; its update returns package-state `9` while work remains and `4` on completion. Its only direct constructor caller is the `StreamPackage` construction path, which allocates and stores this state alongside the other per-package states. Thus the inspected `Loading` symbol is **not a whole-screen loading transition** and must not trigger a device-wide drain.

The D3D9 timing records now include exact integer `total_ns` and `max_ns` alongside rounded display values. `tools/analyze_d3d9_timing.py` validates those fields and known operation names, aggregates exact totals/ratios with per-window provenance and input hashes, preserves residual/final records, and reports relaxed-counter anomalies without adding nested durations into a fake total. Its 24 tests reject 12 malformed-input classes. Source review also rejects a blanket state-call early return: `SetIndices`, vertex declarations, and shaders already deduplicate; `SetStreamSource` already avoids unchanged Vulkan rebinding but must refresh upload-active masks after buffer writes. The next capture must use `changed/calls` and lock/total measurements to decide whether any further state optimization has a real opportunity.

The existing opt-in `import_profile` now also captures the guest return address **before** each D3D9 import pops it. A per-thread 2048-entry bounded accumulator records immediate caller RVAs; overflow and callers beyond the emitted top three remain in exact unlisted residuals rather than disappearing. Only the existing top-ten methods per thread/window emit caller lines, and the ordinary non-profiling dispatch branch is unchanged. `tools/analyze_import_callers.py` validates listed-plus-unlisted accounting and symbolizes a return RVA only when `caller_rva - 1` lies inside a unique Ghidra function extent; external, unresolved, and ambiguous callers stay explicit. This is immediate-call-site attribution, not a guest stack or root-cause claim. Validation covers 24 structural checks/six mutations, UBSan cap/overflow/reset tests, 22 analyzer checks, and an actual C++ formatter → analyzer → real `functions.csv` golden path.

The next gate is an exact-head CI/Android build followed by a matched device capture. No changed-state reduction, query fix, lock fix, or renderer optimization is justified until those records show material cost and the candidate passes correctness and visual gates.

## Immediate next experiment

The next decisive artifact is a **matched warm ordinary-play baseline on the exact candidate**, followed by a separate cold/traced run using the new outcome and protected-path attribution. Those results decide whether the first production candidate should be a loading-screen warm-cache drain, submission batching/culling, a specific lock/query fix, or GPU/renderer work. Until then, assigning a percentage FPS gain to any of them would be fabrication.
