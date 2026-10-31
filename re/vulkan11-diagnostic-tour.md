# Vulkan 1.1 compatibility and diagnostic-tour research (0.14)

Status: implementation candidate only. This is not device validation, a release approval, or proof of visual correctness.

## Method and source inventory

The investigation follows LEANREA's copy, inventory, targeted-probe, and evidence-label workflow. Original game files and the `TestingFiles` ref were treated as read-only. Work in this repository preserves the existing 0.14 updater and DF-format changes.

References inspected:

- `aicodeking/leanrea`, especially `leanrea/SKILL.md`.
- GameNative/Mesa `wrapper-25`, commit `6bcfbaa701a8d1efa7f6c54145495eda5329f767`.
- GameNative/Mesa PR #33, merged as `1bd4f9d8976e01e8da05b64cad41a3af1bbf29bc` on 2026-10-27.
- `utkarshdalal/GameNative`, commit `0755b1f9d5471cbe8878a4dd7d11d1773eabeb33`.
- DXVK 2.5.3, commit `c707d9026f33b6ab89639f154b6ac5f6326fa037`.
- Rick's Game Stuff's contemporary Prototype command-line documentation: https://blog.gib.me/2009/06/21/prototype-command-line-arguments/
- ProtoDroid's original `prototypeenginef.dll` (SHA-256 `d331be205bdde99897952ee88261ab7e63c5c65e36a4660e2a367886a3537071`), generated recompilation, `re/functions.csv`, and `re/subsystems.md`.

## Seen

### Baseline Vulkan path

- Direct Vulkan 1.3 is preferred. The existing Android DXVK patch also has a direct Vulkan 1.2 KHR/EXT compatibility path.
- `NegotiateVulkanApi()` caps negotiation at the tested 1.3 path and rejects a direct result below 1.2.
- Non-Adreno Auto's direct path alone fails below 1.2; the candidate policy below adds its wrapper fallback. Adreno Auto retains its established Turnip order; explicit System, Turnip, and Custom/PanVK modes are separate choices.
- `libturnipprobe.so` already executes driver probes in a fresh process. `LoadDirectIcd()` resolves `vk_icdGetInstanceProcAddr`, and DXVK's Android patch accepts that exact function pointer through `DXVK_VK_GIPA`.
- The supplied working logs are from Mali-G720 MC7 with Vulkan 1.3.278. They are not a Mali-G57 failure log.

### Wrapper mechanism and build constraints

- The Mesa wrapper exports `vk_icdGetInstanceProcAddr` and, without an adrenotools driver environment, opens the system Vulkan loader.
- `wrapper_core13_wanted()` leaves a base Vulkan 1.3 device alone. On an older base device, its default DXVK/vkd3d route requires an appropriate engine name and an application API request of at least 1.3. ProtoDroid's probe therefore supplies `pEngineName = "DXVK"`.
- The inspected wrapper includes API/feature/extension promotion, format/property synthesis, SPIR-V lowering, demote emulation, dynamic-state variants, dynamic-rendering lowering, and legacy copy/synchronization translation.
- GameNative's convenience build is a Termux/X11 build (`-U__ANDROID__`, `-D__TERMUX__`, a Termux sysroot, and an adrenotools subproject pinned to moving `HEAD`). It is not a valid APK build recipe to copy unchanged.
- Mesa's wrapper forces its Android stub configuration. ProtoDroid's candidate build uses `platforms=android`, API 26, static C++ runtime, no X11/Termux sysroot, and no adrenotools loading path. The compatibility wrapper is only around the system Vulkan driver.
- The inspected wrapper hard-coded GameNative cache/diagnostic paths, and its GPU BC pipeline cache ignored `WRAPPER_CACHE_PATH`. The candidate patch uses ProtoDroid app-specific defaults, honors the selected cache root for that cache, routes auxiliary logs through `WRAPPER_WORK_DIR`, and makes `wrapper_diag_append()` honor `WRAPPER_DIAG_FILE` without duplicating redirected stderr. The regenerated patch applies to the pristine pin; the resulting Android binary is not yet built.
- Exact-commit ARM64 CI compiled and installed DXVK before its first Mesa setup failed on duplicate buildtype arguments. After that correction, the next run reached Mesa but required desktop `libdrm` and discovered host zlib/zstd. The wrapper-only build now excludes DRI2 only for Android with no DRI/GBM or other Vulkan driver, confines `pkg-config` to the cross prefix, disables optional zstd, and leaves zlib to an Android system or pinned fallback. This correction also awaits exact-commit CI.
- DXVK 2.5.3/D3D9 requires more than an API-version string. Its required features include core D3D9 features, `shaderDrawParameters`, `vulkanMemoryModel`, `hostQueryReset`, `timelineSemaphore`, `synchronization2`, `dynamicRendering`, and `shaderDemoteToHelperInvocation`, among others checked by the probe. Effective 1.3 also requires `maintenance4`; the Android 1.2 path instead requires its dynamic-rendering, synchronization2, copy-commands2, and extended-dynamic-state extensions and KHR/EXT feature structs. ProtoDroid's Android patch makes BC optional through native DXT decoding and clamps robustness2 with a dummy-resource path, so BC/null descriptors are logged rather than incorrectly required by the wrapper probe.

### Engine entry and diagnostics

- `EngineInitialize` receives a command line. The engine contains `Autotester`, `WorldTest`, and `manhattan` references, but no proven ordered gameplay feature tour was found.
- `re/functions.csv` identifies `proto::InGameState` simulation and real-time methods at DLL VAs `0x104e45c0` and `0x104e4ad0`. Observing their dispatch provides a gameplay signal without editing generated engine code.
- Existing native facilities provide ordinary SDL key injection, DirectInput state, real-present timing, detailed memory/thread/CPU diagnostics, pipeline tracing, presentation timing, file tracing, audio timing, and crash logging.
- Normal `ConfigureSaveDirectory()` may migrate both legacy app-specific saves and previous shared saves. A tour must bypass migration, not merely select another destination.

## Documented, but not locally validated

- `Level=manhattanTest NoAutoSave`: the contemporary command-line reference describes `manhattanTest` as direct gameplay with upgrades unlocked and without missions, infected, or military. `NoAutoSave` is also documented.
- The DLL contains `manhattan`, not the literal `manhattanTest`. No retail archive is available locally to prove that this exact level exists and loads in the user's data.
- GameNative/Mesa PR #33 reports DXVK/vkd3d on Vulkan 1.1 Mali, including a Galaxy M32 G57 r26p0 test. That is evidence for the approach, not evidence for ProtoDroid, DXVK 2.5.3, this APK, or the affected phone.

## Implementation policy

1. Auto preserves direct 1.3 and direct 1.2. Established Adreno/Turnip and explicit Custom/PanVK choices remain explicit.
2. Only Auto's otherwise-unusable direct system path tries the bundled wrapper. The wrapper is isolated-probed first with a DXVK/1.3 request and required-feature checks, then its exact ICD entry point is passed to DXVK. Vulkan 1.0 base loaders/devices are rejected; this candidate is scoped to 1.1+.
3. Base and effective versions/capabilities are logged separately. Probe and runtime wrapper logs/caches are separate, tour paths remain isolated, and expensive `WRAPPER_DIAG` plus shader/BC output is tour-only (not a full API dump). Probe or feature failure names the failed route; a real in-process wrapper crash leaves a build-specific recovery marker to prevent an automatic crash loop.
4. The tour is deliberately armed in the launcher by typing `MERCER` or tapping the Prototype logo seven times. Arming is visible and reversible and applies only to the next Play launch.
5. The tour requests `Level=manhattanTest NoAutoSave`, uses isolated saves/logs/cache, performs no normal-save migration, and waits for observed `InGameState` dispatch before beginning actions. Shared records live in `Prototype/diagnostic-tour/logs`; its app-private native log and DXVK cache are also separate from ordinary Play. A final small DXVK patch routes pipeline traces/SPIR-V dumps to that tour directory while preserving the ordinary trace default.
6. Ordinary SDL input drives short sequential phases with real-present dwell. Durable records distinguish requested, running, tested, skipped, aborted, and failed. `tested` means the scripted actions ran; it never means the visual result was verified.
7. A crash marker records the last durable phase. Startup/level failure, unavailable gameplay, stalls, marker-write failures, and unsupported actions are reported rather than converted into success. Normal launches clear tour-only diagnostics and any stale Vulkan GIPA handoff before applying their own settings.

## Inferred

- The wrapper candidate is the most direct reusable implementation of the requested Vulkan 1.1-to-DXVK lowering, but its Android/APK adaptation and exact feature coverage must be established by the candidate build and device logs.
- `manhattanTest` is the best documented direct-gameplay candidate for sequential input/render tests. Actual retail behavior determines whether the tour runs or reports an entry failure.
- Camera, movement, combat, and power-adjacent inputs can exercise rendering, shadows, particles, lighting, assets, and audio together. Their visual outcome cannot be inferred from input dispatch or script completion.

## Unknown / still requires physical validation

- Whether the pinned wrapper builds cleanly with Android WSI and passes DXVK 2.5.3 on the affected Mali-G57.
- The affected phone's loader/device versions, extension set, feature set, missing capabilities, wrapper effective capabilities, stability, and performance.
- Whether the user's retail data contains and successfully enters `manhattanTest`, and how soon its camera/input are ready.
- Exact power activated by each physical input under the user's bindings.
- Dynamic-shadow correctness, particles, lighting, asset streaming, audio/video synchronization, and all other visual/auditory results. Screenshots and human/device review remain necessary.
- Universal Vulkan support, stable 60 FPS, and complete crash coverage are not claimed.
