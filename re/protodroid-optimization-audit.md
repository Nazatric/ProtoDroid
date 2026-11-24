# ProtoDroid optimization audit

This note separates source-level findings, implemented changes, and device evidence. Repository issues and community reports are problem statements, not controlled benchmarks. No result below should be read as a universal FPS, temperature, battery, or compatibility claim.

## Summary

Implemented in this audit:

- Android native CI builds now use a bounded, Actions-cached `ccache` (2 GiB) for both shipped ARM64 engine variants. The exact source revision remains embedded in the runtime logs, but its compile define is limited to `main.cpp` and `diagnostic_tour.cpp`; otherwise every object would miss the cache on every commit. Cache statistics are emitted by the workflow so the first cold run and subsequent warm runs can be distinguished.
- The controls screenshot test keeps its `ui_shot_now` → `ui_shot.png` behavior but uses a directory `FileObserver` instead of checking the filesystem once per second. The 500 ms FPS-overlay callback is now scheduled only while the overlay is enabled. Both are stopped with view detachment; a screenshot trigger remains pending if the view has not acquired dimensions yet.

No DXVK compiler-worker, shader-precompile, render-quality, driver-selection, or APK-library changes were justified by the supplied evidence. The current branch already defaults to a 30 FPS engine-loop cap, but an explicit `fps_limit=0` in the supplied device run overrides that default. No on-device power or gameplay-performance gain has been measured.

## Findings and dispositions

### Power and frame pacing — issue #54

The supplied `proto-native-30.log` identifies device `2412DPC0AI` (`rodin`), MT6899, Mali-G720 MC7, and reports `fps_limit=0`, FIFO presentation, and a 16.667 ms ADPF target. It includes diagnostic-tour phases with a 4× guest clock and idle/test intervals, so its 141 one-second windows are not a representative normal-gameplay benchmark. The 30 FPS default already present on this branch does not apply when the user's saved/configured value is explicitly Off. Thermal status was `none`; CPU/GPU headroom was unsupported. These records do not measure battery drain or establish a before/after gain.

**Disposition:** preserve explicit user overrides and FIFO; do not silently clamp Off or force device-specific settings. The only new runtime work reduction here is removal of idle UI polling. A useful power comparison must use the same device, driver, resolution, scene, brightness, audio, and thermal starting state, with a saved setting that actually exercises each cap; report energy or battery power, thermal state, frame pacing, and wall-clock duration together.

### Shader and pipeline stutter — issue #51

`app_process64_d3d9-26.log` says no DXVK state-cache file was present at startup and that DXVK subsequently created one. The sampled graphics-pipeline driver averages were roughly 9–45 ms, with maxima around 34–62 ms and no pipeline errors. This supports testing cold versus warm startup, but does not prove any individual hitch was caused by compilation or that the supplied run exercised a warm cache. The ordinary DXVK cache is stored under app-specific external files; the diagnostic tour deliberately uses a separate cache. The pipeline trace contains 310 sequences; its first sequence has no SPIR-V and is marked structurally invalid, while later records contain nonzero SPIR-V. That is a trace-format/inspection lead, not evidence of a corrupted runtime shader.

**Disposition:** keep the existing cache validation, persistence, and diagnostics. Do not bundle an invented universal cache seed or precompile unobserved variants. DXVK documents that the state cache can replay previously seen state, not predict every future variant; graphics-pipeline-library support is driver-dependent. Validate with consecutive normal-play runs on the same driver/cache: capture first-use hitches and compiler activity on the cold run, then compare a warm run without clearing the cache. Keep the diagnostic tour out of that A/B.

### Touch controls and repeated UI work — issues #45, #46, #53

`TouchControls.java` already uses an indexed key catalog, cached drawing objects/text, and avoids full overlay invalidation for camera-only movement. The screenshot trigger was the remaining one-second filesystem poll in ordinary gameplay. The source also scheduled a 500 ms FPS refresh even with the overlay off. These callbacks did not establish a measured battery cost, but were unnecessary recurring work.

**Disposition:** preserve the screenshot diagnostic using `FileObserver` events (`CLOSE_WRITE` and `MOVED_TO`), and keep the 2 Hz refresh only while FPS is visible. A trigger created before attachment or before the view has dimensions remains recoverable. The Enter button is already defined as `KEYCODE_ENTER` and routes through the shared pressed-key state into SDL; static routing cannot verify the reported mission interaction on the affected game state. Wired keyboard/mouse and the Redmi Note 12 report likewise need device confirmation; no compatibility claim is inferred from source support alone.

### Android native CI time

The two prior CI runs spent approximately 1,461 s and 1,098 s in the native `libmain.so` build step, while APK packaging/signing took about 43 s and 34 s. Native compilation is therefore the high-leverage repeat-build target. The build deliberately remains `-j1` because its existing workflow selects that policy for memory safety; this audit does not increase parallelism or risk another OOM. Ccache hashes compiler inputs/options and the included dependency contents, with a content-based compiler check and a 2 GiB bound. The cache is correctness-neutral: a miss only recompiles, and a hit must match the compiler, flags, source, and dependencies.

**Disposition:** record `ccache --show-stats` and compare a cold build with a later run restored from the cache before claiming a time reduction. The signed package still verifies that both native ARM64 variants contain the exact current source revision. The cache is CI-only and does not change runtime performance.

### APK size

The previous `prototype-apk` Actions artifact predates this audit (run `45255072641`). Its artifact archive is 58,054,846 bytes; the prior artifact inventory listed the APK at 58,051,452 bytes. Debug symbols are a separate artifact and are not installed in the APK. `android/build-apk.sh` already strips every packaged shared library and uses DEFLATE level 9. The APK includes distinct ARMv8.2 and ARMv8.0 engine binaries plus bundled Vulkan/driver components that the app currently supports.

**Disposition:** no library was removed and no quality, compatibility, or diagnostic symbol policy was weakened. Moving optional drivers out of the base package or adopting split delivery would be a product/installation design change, not a safe local size tweak. The artifact archive could not be downloaded into this audit runner (the GitHub artifact-storage request ended with `EOF`), so no fresh binary-section attribution is claimed. Reassess the exact new APK and its package metadata after CI.

### Logging and device-specific reports

The ordinary DXVK state-cache line, pipeline timing/trace, native diagnostics, and crash artifacts were retained. No supplied evidence justified reducing those logs or changing shader correctness behavior. Existing issue reports remain individually bounded as follows:

| Issue | Evidence-based disposition |
| --- | --- |
| [#49](https://github.com/Nazatric/ProtoDroid/issues/49), Mali-G52 / Android 11 wrapper load | The current branch includes the private-library dependency fix and policy test: private Mesa stubs are static and the wrapper uses public Android libraries. Affected-device retest is still needed. |
| [#50](https://github.com/Nazatric/ProtoDroid/issues/50) and [#48](https://github.com/Nazatric/ProtoDroid/issues/48), Snapdragon startup | #50 explicitly records a Vulkan 1.1 loader/system path where DXVK needs newer capabilities; #48 remains a device startup report without a confirmed fix here. Existing capability validation must remain truthful; neither is a claim that those devices now reach gameplay. |
| [#47](https://github.com/Nazatric/ProtoDroid/issues/47), PowerVR BXM-8-256 | The issue log shows Vulkan negotiation and then D3D9 `CreateDevice` failure. Without the corresponding driver error and a device reproduction, no graphics workaround is justified. |
| [#44](https://github.com/Nazatric/ProtoDroid/issues/44), movie/audio and widescreen bars | Treat audio/video timing and aspect presentation separately. No controlled media file/device comparison in the supplied evidence isolates the cause. |
| [#45](https://github.com/Nazatric/ProtoDroid/issues/45), mission Enter | The touch key and SDL/DirectInput route exist in source; the issue remains open pending a reproducible interaction trace or affected-device retest. |
| [#46](https://github.com/Nazatric/ProtoDroid/issues/46), midrange/Mali performance and layout | Qualitative request plus a maintainer's report on another Mali device is not a comparable benchmark. No Mali-wide compiler, render-quality, or FPS policy was copied from it. |
| [#53](https://github.com/Nazatric/ProtoDroid/issues/53), Redmi Note 12 and wired input | Device/OS, connection details, and a diagnostic run are needed before a compatibility change. SDL input support alone is not hardware validation. |
| [#54](https://github.com/Nazatric/ProtoDroid/issues/54), power use | Default cap already changed on this branch; the supplied explicit-Off run does not test it. Battery and thermal A/B remains outstanding. |
| [#51](https://github.com/Nazatric/ProtoDroid/issues/51), shader precompilation | The cache exists and is populated after first use; a warm-cache A/B is outstanding. No unverified full-game precompile pass was added. |

## Technical sources and community context

- Android frame pacing and refresh-rate guidance: [Frame Pacing](https://developer.android.com/games/sdk/frame-pacing) and [Slow sessions](https://developer.android.com/games/optimize/vitals/slow-session).
- Android Performance Hint API: [NDK reference](https://developer.android.com/ndk/reference/group/a-performance-hint).
- Vulkan persistent pipeline-cache example and compatibility discussion: [Vulkan Samples](https://docs.vulkan.org/samples/latest/samples/performance/pipeline_cache/README.html).
- Pinned graphics translation layer behavior: [DXVK v2.5.3 README](https://github.com/doitsujin/dxvk/blob/v2.5.3/README.md).
- Custom-view invalidation guidance: [Android custom views](https://developer.android.com/develop/ui/views/layout/custom-views/optimizing-view).
- Package inspection and size guidance: [Reduce app size](https://developer.android.com/topic/performance/reduce-apk-size).
- CI cache semantics and compiler-cache correctness: [GitHub Actions dependency caching](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching) and [ccache manual](https://ccache.dev/manual/latest.html).
- Issue reports are the directly relevant community signals. Broader Android-emulator/Windows-game community examples reviewed include a [GameNative settings discussion](https://www.reddit.com/r/GameNative/comments/1ugkl4g/how_to_optimize_your_gamenative_settings_and/) and an [Android-emulation video](https://www.youtube.com/watch?v=wdGQ3np9E-0); both are anecdotal and device/app-specific, so they were not used as ProtoDroid tuning evidence or performance benchmarks.

## Validation and remaining acceptance criteria

Source checks run locally for the new compiler-cache keying and touch-control callback lifecycle, along with the existing runtime-performance, Vulkan-wrapper, and release-workflow policy tests. Full Android Java compilation, NDK compilation, signed-APK verification, cache-hit/time comparison, and all device claims depend on the next GitHub Actions run and physical-device testing. Keep PR #55 open and draft; do not merge or release.
