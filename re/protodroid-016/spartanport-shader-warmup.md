# SpartanPort startup/background shader warm-up — applicability audit

Status: **source and focused-helper audit complete; no ProtoDroid runtime change adopted**. The user supplied SpartanPort as a possible answer to startup shader compilation. The inspected mechanism is real, but it is not a drop-in DXVK patch and its default is background warm-cache loading rather than unconditional blocking compilation at startup. No phone FPS, loading-time, or stall improvement is inferred.

## Exact source and provenance

- Repository: https://github.com/FelipePrado0/SpartanPort
- Inspected commit: `74b06dffa9c24a4d63df3daf714853fbdccc5b7d` (`main` at inspection, commit time `2026-11-04T22:16:11-03:00`).
- Inspected files: `gpu/shadps4/video_core/renderer_vulkan/vk_presenter.cpp`, `vk_pipeline_cache.{h,cpp}`, `vk_pipeline_serialization.cpp`, `vk_warmup_inbox.h`, `tests/test_warmup_inbox.cpp`, `shader_recompiler/profile.h`, and the README's background-compilation description.
- The inspected implementation headers carry `GPL-2.0-or-later`; the root contains GPL v2 text. No source code was copied into ProtoDroid. Any future copying requires a separate license/attribution review; a clean-room design decision does not erase this provenance.
- Comparison dependency: DXVK v2.5.3 `c707d9026f33b6ab89639f154b6ac5f6326fa037`, plus ProtoDroid's ordered Android `dxvk-*.patch` series at this PR head. The disposable comparison clone is outside Git and all 16 patches applied cleanly.
- External clone: `/home/user/protodroid-016-research/spartan-port`; comparison clone: `/home/user/protodroid-016-research/dxvk-spartan-comparison`. Helper binaries/results are under `/home/user/protodroid-016-evidence/continuation/spartanport-shader-warmup/`.

## What SpartanPort actually does

`Presenter::Presenter` first tries its capability-gated on-demand pipeline-binary path. Otherwise, unless `SPARTAN_BACKGROUND_WARMUP=0`, it calls `PipelineCache::StartBackgroundWarmUp()` and returns to gameplay when that path is available. Only when background warm-up is disabled or unsupported does it display a loading frame and call the blocking `PipelineCache::WarmUp()`.

The warm-up is **warm-cache reconstruction**, not discovery or compilation of every shader the game will ever use:

1. `CheckCacheProfile()` checks that caching is enabled and that the serialized `Shader::Profile` matches the current profile. An absent or incompatible profile produces no warm-cache preload.
2. `WarmUp()` and `ReadWarmEntry()` iterate previously stored `PipelineKey` records. They load saved shader metadata and saved SPIR-V (`ShaderMeta` and `ShaderBinary` blobs), create shader modules with `CompileSPV`, and construct real graphics/compute pipeline objects from the stored pipeline keys/state.
3. `WarmupPool` builds pipeline objects on worker threads. The background reader/module path remains ordered where the program cache is not thread-safe; the GPU thread merges stage ownership and publishes results in bounded batches.
4. `PumpWarmUp()` consumes at most 32 read entries per approximately 4 ms batch and at most 64 built results per call. `PublishFirst` keeps the first valid pipeline for a key, so a runtime compilation and a late warm-up result do not replace one another. Invalid warm-up builds remain unpublished and can be retried by the normal runtime path.
5. `WarmupThreadCount()` selects one thread on four or fewer cores, otherwise all but two cores capped at six, with low-priority background workers. That number is SpartanPort policy, not a safe ProtoDroid default.

Consequently, the startup screen does not prove that first-use GCN translation or every future pipeline was eliminated. A cold cache still requires normal runtime shader/pipeline creation, and a warm cache can only rebuild variants actually serialized earlier. The README itself warns that background compilation can still cause stutters, missing effects during compilation, and temporary FPS drops. Blocking startup warm-up moves compilation latency to startup rather than making the total work free.

## Focused source execution

The unchanged `tests/test_warmup_inbox.cpp` was compiled directly against the pinned `vk_warmup_inbox.h` with GCC/C++20, once optimized and once with ASan/UBSan. Both runs print `PASS: warm-up inbox`; sanitizer stderr is empty. The checks cover thread-count bounds, concurrent producer inbox integrity, bounded FIFO batches, first-valid-pipeline publication, refusal of invalid pipelines, and preservation of runtime refusal markers.

This validates only the small helper policy at the pinned source. It does not execute Vulkan, a driver, the full SpartanPort renderer, ProtoDroid, or a physical Android device. Commands, binary/source hashes, stdout, and sanitizer stderr are retained in the external evidence directory above.

## ProtoDroid/DXVK already has part of this mechanism

The important baseline equivalent is documented in DXVK's own `DxvkStateCache` comment: cached state allows pipelines to be compiled ahead of their first draw.

- `DxvkStateCache::readCacheFile()` reads versioned, SHA-1-validated pipeline entries and maps shader keys to cached pipeline state.
- When D3D9 creates a shader, `D3D9ShaderModuleSet` calls `DxvkDevice::registerShader`; `DxvkPipelineManager::registerShader` then reaches `DxvkStateCache::registerShader`.
- Once every shader required by a cached entry is registered, `registerShader` queues that real entry and `compilePipelines` sends it to `DxvkPipelineWorkers::compileGraphicsPipeline` at normal priority. The worker pool compiles in the background at lowest thread priority.
- ProtoDroid already bounds this pool to two workers on Mali and serializes affected Mali `vkCreateGraphicsPipelines`/`vkCreateComputePipelines` calls because concurrent proprietary-driver compilation is unsafe there. SpartanPort's six-worker policy must not override these guards.

Therefore **background compilation of known warm-cache pipelines is already present** in ProtoDroid's patched DXVK baseline. Adding a second independent compiler/cache would duplicate work and risk incorrect lifetime and publication behavior.

## What is not currently equivalent

DXVK's state-cache entries contain shader keys and pipeline state, not SpartanPort's self-contained shader metadata plus saved SPIR-V blobs. DXVK cannot reconstruct every cached shader solely from its state-cache file at process start; it waits for the game/D3D9 path to register the corresponding real shader objects. Shader registration can itself occur progressively during gameplay.

That means a literal SpartanPort-style independent whole-cache preload at the launcher is **not** a small UI change or a drop-in function copy. A blocking screen after existing DXVK queues have been populated would also require a reliable engine startup/loading milestone and real queue completion tracking; waiting earlier would compile only the subset whose shaders happen to be registered.

## Feasibility and recommended disposition

**Feasible in principle, but not adopted from this audit.** There are two materially different options:

1. **Use the existing DXVK path better.** Instrument cache-entry reads, actual shader registrations, normal-priority warm-up queue/completion counts, high-priority first-draw compilation, and driver wait timestamps. If cached pipelines are already being compiled before gameplay, a new preloader is redundant. An optional wait/progress screen could be considered only after a real loading milestone and measured benefit exist; default background behavior should remain unless A/B evidence supports otherwise.
2. **Add independent whole-cache reconstruction.** This would require a deliberately versioned cache extension containing sufficient real shader data/metadata, exact app/driver/settings/capability invalidation, bounded low-priority workers, first-valid publication, corrupt/partial/read-only fallbacks, and the existing Mali serialization. It must create only previously observed real pipelines—never imaginary variants, placeholder shaders, skipped draws, or unconditional rendering before valid pipelines exist. This is a substantial cache architecture change, not a quick port of SpartanPort's PS4/GCN implementation.

The next justified experiment is **measurement first**, not a production patch: quantify how many of ProtoDroid's 1,014 tracked-cache entries become compilable before first gameplay, when their shaders register, and where first-draw driver stalls remain. Then choose between a narrow existing-worker scheduling/UI change and the larger self-contained cache design. Cold and warm runs must be separate, with raw startup/first-encounter timestamps, frame-time p50/p95/p99/max, visual/output correctness, cache-corruption cases, and per-driver results. Until then, any startup or shader-stall improvement is **inconclusive**.
