# External repository ledger

**Phase 2 is active.** This is a substantive source-audit checkpoint, not a completion or device-validation claim. All five required repositories were pinned and inspected. Dispositions below are audit-level decisions; a technique marked for a later experiment still requires ProtoDroid profiling and controlled correctness/performance evidence.

No external source code has been copied into ProtoDroid. The read-only, sparse, blob-filtered clones are outside Git under `/tmp/protodroid-016/external`; file lists, per-file SHA-256 manifests, commit provenance, key-file hashes, license hashes, and the shared-lineage comparison are under `/tmp/protodroid-016/audit`. `/tmp/protodroid-016/audit/audit-manifest-sha256.txt` hashes those manifests. These paths are reproducibility records, not files available on another machine.

## Pinned sources

| Repository | URL | Branch and exact commit | License/provenance at the pin | Review state |
| --- | --- | --- | --- | --- |
| PrototypeRecomp | https://github.com/Nazatric/PrototypeRecomp | `main` — `c9224ecd1b7c422d5fa498c5fd541fabeb38858f` | No license file found in the pinned tree. README describes an ARM64 Android/Vulkan target as work in progress; implementation is Xbox 360/PowerPC. | Runtime, GDFX, filesystem/APC, GPU-packet, threading, and build paths inspected; semantic reference only |
| Black-mobiu | https://github.com/contatg/Black-mobiu | `main` — `eaab755566185f4d271c71470d6b35d26a4e59aa` | MIT `LICENSE`, SHA-256 `844e017c30b7d78d1484c91c4babe9906df5d9cd800613284ee1c4b0dbc4f609`; attribution is required for copied MIT code. | Shader cache/translation, Vulkan capability selection, pipeline persistence, batching, state delta, and synchronization inspected |
| gow3_android_daedalus_fork | https://github.com/erickmendes22/gow3_android_daedalus_fork | `android` — `b3a7c3d5e916cf8f4622192c0faa9383ebd77c00` | GPL v2 or later; `LICENSE` SHA-256 `edaef632cbb643e4e7a221717a6c441a4c1a7c918e6e4d56debc3d8739b233f6`, renderer headers say `GPL-2.0-or-later`. README credits zFitness, GoW PC forks, shadPS4, and FEX. | Worker queues, pipeline caches/serialization, draw preparation, scheduler, staging, HLE copy path, Android settings, and PC/Android documents inspected |
| iss_android | https://github.com/erickmendes22/iss_android | `main` — `43c833deb37bc504bf099bbf707a5440103a8cae` | No root license file; `port/LICENSE` is GPL v2 text with the same SHA-256 above and renderer headers say `GPL-2.0-or-later`. `docs/ANDROID_PORT.md` identifies upstream `HayatoG/iss_pc` at `1898d449a7edf506d12ef63445f8fc5432e60105`. | Android preparation documents plus inherited async compiler, draw preparation, staging, pipeline serialization/cache, and renderer inventory inspected |
| Bloodborne-Android | https://github.com/ducvd89/Bloodborne-Android | `master` — `f241353e36f46bf183806acd7b79222afff2fd2f` | GPL v2 or later project; the same GPL v2 `LICENSE` hash, source headers `GPL-2.0-or-later`, and additional whole-application/third-party notices. README credits the PC/shadPS4 lineage. | Strict AOT dispatch, unified-memory buffer paths, wait strategies, staging, Android support limits, and device/session documents inspected |

ProtoDroid itself has no root license file in this checkout. That does not grant permission to relicense external work. Direct incorporation of any external implementation remains blocked pending an explicit license/attribution review; clean-room implementation of a generally applicable idea is a separate decision and must still preserve provenance in the experiment record.

## 1. PrototypeRecomp — semantic reference, not a performance patch source

### Code traced

- `runtime/gdfx_volume.cpp::GdfxVolume::Read` performs a binary search over sorted disk regions and positioned `pread` calls while holding `io_mutex_`. This is real region lookup and host I/O, not a claim inferred from the README.
- `runtime/imports_fs.cpp::NtReadFile`, `NtWriteFile`, and `NtReadFileScatter` complete the host read/write and IOSB before `QueueIoCompletionApc`; only then do non-synchronous handles return `X_STATUS_PENDING` and signal completion. The guest-visible asynchronous APC/event ABI is implemented, but there is no host asynchronous I/O worker hidden behind it.
- `runtime/gpu_command_processor.cpp::XenosCPMain` has a real command-processor thread, ring/doorbell handling, packet counters, wait handling, and interrupt dispatch. Draw and shader opcodes remain a frontier in the audited implementation; this is not an Android Vulkan renderer whose batching can be benchmarked as ProtoDroid evidence.
- `runtime/state.cpp` and `runtime/imports_vd.cpp` implement guest threads/APCs and Xbox display/ring semantics. PowerPC/Xenon behavior must not be conflated with ProtoDroid's existing x86 static recompilation.

### Evidence and applicability

The README's Android/Vulkan wording is a target, while the pinned tree explicitly remains WIP. No controlled Android FPS result was found. GDFX region search is not a reason to replace ProtoDroid's existing RCF index, and synchronous host reads followed by guest `PENDING` semantics are not an asynchronous-I/O optimization. **Disposition: useful for semantic comparison; no 0.16 production technique adopted.**

## 2. Black-mobiu — real implementations in a different D3D8 renderer

### Code traced

- `native/vk_shader.cpp`: versioned SPIR-V filenames, cache load validation, temporary-file-plus-rename stores, and a mutex around glslang translation. On a miss, HLSL parsing/linking/SPIR-V emission is synchronous; this is not an asynchronous shader compiler.
- `native/vulkan.cpp`: Vulkan 1.1–1.3 selection, extension checks, `VkPhysicalDeviceFeatures2` feature queries, format-feature checks, dynamic-rendering/timeline fallbacks, and real `vkCreateGraphicsPipelines` calls. `SavePipelineCache` serializes the driver cache; `Present` calls it every 600 presents. Idle cache work is separately bounded around 600 idle frames.
- `src/d3d/render_thread.cpp`: 64-command batches, recycled `Batch` objects, a worker queue, condition-variable waits, 64-byte state-delta pieces with NEON comparisons, resource-use tracking, and conditional readback only for surfaces written by the GPU.

### Evidence and applicability

These mechanisms exist in code, but this repository supplies no matched ProtoDroid/DXVK measurement. Its renderer and resource model are not drop-in replacements for Android-patched DXVK 2.5.3. Capability/fallback structure is a useful correctness reference; batching, state delta, and cache cadence remain candidates only if ProtoDroid profiles show equivalent costs. **Disposition: selectively useful ideas, no wholesale renderer or synchronous shader path import.**

## 3. GoW III Daedalus fork — substantial code, mixed PC and Android evidence

### Code traced

Under `gpu/shadps4/video_core/renderer_vulkan/`:

- `vk_async_compiler.h`: bounded normal/urgent queues, worker initialization, queue-wait counters, stop/drain behavior, one-job-per-entry futures, and recyclable compiler pools. Queue-full submission fails so the caller can use its fallback.
- `vk_pipeline_cache.cpp`, `vk_pipeline_serialization.cpp`, and `driver_cache_file.h`: runtime/warm-up pipeline creation, per-worker caches, merging/snapshotting, optional pipeline-binary persistence, driver identity/header/checksum validation, and atomic cache-file replacement.
- `vk_draw_prep.cpp` and `vk_draw_prep.h`: speculative command/register/resource preparation with GPU-thread validation and fallback. This is correctness-sensitive work, not unconditionally trusted stale guest data.
- `vk_scheduler.cpp`, `vk_draw_pipe.h`, and related recorder paths: segmented recording, worker handoff, ordered execution, and drains for non-pipelineable operations.
- `vk_staging_buffer_pool.cpp`: block reuse and retention. Defaults include eight 16 MiB prewarmed blocks (128 MiB) and a 512 MiB retention setting.
- `vk_shader_hle.cpp`: game-specific MultiCopy/HLE work, not a generic D3D9 optimization.

### Evidence context

- `docs/parallel_gpu.md` contains real PC test-scene A/B numbers, including roughly +18–32% for stages of the draw pipeline, +11% for the constant ring, +9% for render-state memoization, and +5.6% for texture-set memoization. The document also records neutral and slower experiments. These measurements use desktop/high-thread/RX 7800 XT contexts and often FSR/test-specific settings; they are not Android or ProtoDroid gains.
- `docs/ANDROID_PERFORMANCE_DELTA_025.md` explicitly says its investigation is not a measured Redmagic FPS gain. It disables Android speculative draw-preparation workers by default, retains two Android compiler workers, and calls the two-queued-frame policy a latency/throughput tradeoff requiring controlled validation.
- `docs/ANDROID_BACHATA_OPTIMIZATIONS.md` describes ARM64 SRT and driver-cache tests but likewise says code tests do not establish an FPS gain. `docs/ANDROID3_PORT.md` leaves hardware validation to the user.

### Applicability

The bounded-queue/backpressure, validation, and low-contention principles are useful. ProtoDroid already caps Mali DXVK compiler workers at two and serializes affected Mali driver pipeline calls, so importing more workers would not reproduce the GoW III architecture or prove more parallel driver compilation. Large staging prewarm/retention is inappropriate without a memory/profile justification. **Disposition: candidate principles only; no FEX, PS4 renderer, HLE, PGO profile, upscaler, or large-memory default import.**

## 4. inFAMOUS Second Son Android — preparation-only Android state, shared renderer lineage

### Code and documents traced

- `docs/ANDROID_PORT.md` and `README.md` describe a glibc/FEX-first plan, then Bionic. They explicitly record no integrated guest-CPU/FEX implementation, an X11 requirement, no Android target/APK workflow, and preparation-only changes. There is no validated Android gameplay/performance result at this pin.
- Despite that Android status, `port/gpu/shadps4/video_core/renderer_vulkan/` contains real `vk_async_compiler.h`, `vk_draw_prep.cpp`, `vk_pipeline_cache.cpp`, `vk_pipeline_serialization.cpp`, `vk_scheduler.cpp`, and `vk_staging_buffer_pool.cpp` implementations. These cannot be dismissed as empty stubs.
- `/tmp/protodroid-016/audit/shared-lineage.txt` compares corresponding GoW III and ISS files. `SequenceMatcher` ratios are 0.9748 for the async compiler, 0.9987 for the staging pool, and 0.9787 for draw preparation; marker-normalized staging similarity is 1.0000. This demonstrates shared implementation lineage, not independent validation.

### Applicability

The inherited code can clarify implementation differences, but it supplies no second device result and no independent confirmation of the GoW III numbers. Optional `VK_KHR_pipeline_binary` persistence is capability- and driver-dependent and cannot be inferred from extension presence alone. **Disposition: provenance/difference reference only; no direct Android performance claim or duplicate technique count.**

## 5. Bloodborne-Android — controlled Adreno evidence with important negative results

### Code traced

- `cpu/guest_cpu_aot.cpp::Lookup`/`BuildTable`: strict native-unit dispatch using open addressing, multiplicative hash `0x9E3779B1`, linear probing, and a zero empty key. The current file explicitly has no interpreter/JIT fallback; misses stop and are logged.
- `gpu/shadps4/video_core/buffer_cache/buffer_cache.cpp` and `bbport_uma.*`: eligibility probes, read-only per-granule direct bindings, dynamic/write verdicts, demotion/re-promotion, and fallback to copy mode. Shared granules are deliberately not sparse-bound into the main arena.
- `vk_draw_pipe.h`, `vk_bind_helper.h`, `vk_scheduler.cpp`, and `amdgpu/liverpool.cpp`: bounded spin-then-block/wait strategies, wakeups, CPU-time/wait accounting, and core-count-sensitive behavior rather than unconditional yield loops.
- `docs/AOT_DEVICE_STATUS.md`, `docs/DEVICE_TEST_2.0.0.md`, and `docs/SESSION_2026-10-18_UNIFIED_MEMORY_POWER.md`: actual AOT/FEX comparisons, a later strict-AOT session, memory/FPS tests, and failure/thermal notes.

### Evidence context

- AOT versus the earlier FEX runtime at one save was 29.7/29.4 FPS; a later 169-sample strict-AOT session averaged 29.2 game FPS while 60 shown frames used frame generation. Generated frames are excluded from any technique conclusion here.
- Copy mode versus unified-memory work at one controlled spot was 5382→4944 MB PSS at 24 FPS: a memory saving, not an automatic FPS increase. Later blocking-wait/pinning runs used different settings and are not silently merged into that comparison.
- The session document records a Thor hard reset when shared dma-buf memory was sparse-bound through KGSL, a race fixed during 32 MiB-granule testing, and thermal collapse on another device. The README says Mali is unsupported/testing-only at this pin.

### Applicability

ProtoDroid already has the same open-addressed multiplicative-hash dispatch structure in `src/native/runtime.cpp` and `src/recomp/bridge_x86.cpp`; replacing it with the Bloodborne table is not a new optimization. Strict-AOT results caution against assuming a CPU architecture rewrite raises GPU-bound FPS. Unified-memory code is PS4/Adreno-specific and carries serious coherence/driver risk; it is not a DXVK patch. Short-spin-then-block is a possible later scheduling experiment only where ProtoDroid profiles show spinning/wake latency. **Disposition: useful measurement lessons and negative evidence; no AOT/FEX/UMA/affinity/frame-generation import.**

## Audit-level conclusion

The five repositories do not justify a forced patch from each. The best-supported next step is to profile ProtoDroid's existing pipeline compiler, Present/pacing, guest/render threads, native hot paths, and upload/cache behavior before selecting a technique. Several attractive ideas are already present, belong to incompatible renderers, or have neutral/negative evidence in their own source documents. See `technique-matrix.md` for dispositions and `rejected-changes.md` for explicit non-actions.
