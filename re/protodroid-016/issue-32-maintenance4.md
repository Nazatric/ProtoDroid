# Post-Phase-14 follow-up: Android maintenance4 capability selection

Status: **source/host compatibility correction integrated on the working branch after exact CI; issue #32 remains open and unconfirmed**. Production commit `e1277dbc6448c27c10d1ee58070b1eefeb10da85` follows starting head `46af184256c1fb165f59a9ccc8ec6704acc6238e`. This is a follow-up to the closed Phase 14 audit, not a new timed phase, a performance claim, or a release-readiness change.

## 1. Report and diagnostic boundary

Issue #32 reports v0.14 on a Motorola Moto G Play 2024 (Snapdragon 680, Adreno 610, Android 14) using PurpleVK. The supplied log, from reported source `fa8b6e55079ea29f6be00fa0cf6baea57ee56cf7`, ends with `DxvkAdapter: Failed to create device: VK_ERROR_FEATURE_NOT_PRESENT`, followed by the native D3D9 initialization failure.

The log's `Device features` block is the **requested** logical-device state passed to DXVK's feature logger. It is not a physical-device support report. It therefore cannot identify the failing feature, and this follow-up does not infer that #32 was caused by maintenance4. No feature is spoofed, no vendor/model workaround is added, and no issue is closed from host evidence.

## 2. Source-provable compatibility finding

Before this follow-up, the Android-patched DXVK 2.5.3 device-creation path requested `enabledFeatures.vk13.maintenance4 = VK_TRUE` for non-AMD Vulkan 1.3 devices, regardless of `m_deviceFeatures.vk13.maintenance4`. The explicit Vulkan 1.2 compatibility path already followed physical support. The requested-feature audit also treats maintenance4 as required whenever the bit remains enabled, and the isolated wrapper probe rejected a Vulkan 1.3 route without it.

The same pinned DXVK source has complete fallbacks in both memory-requirement functions:

- `DxvkMemoryAllocator::getBufferMemoryRequirements`: without maintenance4, create a temporary buffer, call `vkGetBufferMemoryRequirements2`, and destroy the buffer.
- `DxvkMemoryAllocator::getImageMemoryRequirements`: without maintenance4, create a temporary image, call `vkGetImageMemoryRequirements2`, and destroy the image.
- The Vulkan 1.2 command-alias setup already resolves maintenance4 commands only when the feature is enabled.

Thus, forcing an unsupported maintenance4 bit into `vkCreateDevice` is unnecessary on Android: the established runtime path can operate without it. This finding justifies a capability-driven correction with focused coverage, but does not establish the missing feature in any phone log.

## 3. Exact change

- `android/patches/dxvk-zzzzzzz-vulkan12-profiles.patch`: on Android, both Vulkan 1.2 and 1.3 now copy the queried `m_deviceFeatures.vk13.maintenance4` value. If unavailable, startup logs that temporary-resource memory queries will be used. The existing AMD exclusion is preserved. Non-Android builds retain the prior `VK_TRUE` policy inside that same exclusion.
- `src/native/turnip_load.h`: the isolated compatibility-wrapper probe no longer classifies maintenance4 as a required feature on Vulkan 1.3. Its actual support value remains in the probe report. All other required features and extensions are unchanged.
- `tools/test_dxvk_device_preflight.py`: new checks bind the Android request to physical support, preserve the non-Android branch and conditional Vulkan 1.2 aliases, require both allocator fallbacks, and ensure the wrapper removes only the maintenance4 rejection while retaining diagnostics.

There is no renderer replacement, API-version spoof, quality reduction, disabled effect, blanket feature disable, cutscene change, or performance-sensitive change on devices where maintenance4 is supported. Devices without it use DXVK's existing correctness fallback rather than failing device creation solely for that optional bit.

## 4. Raw validation results

Validation used a fresh DXVK v2.5.3 checkout at `c707d9026f33b6ab89639f154b6ac5f6326fa037` and applied every `android/patches/dxvk-*.patch` in lexical order with `git apply --check` before each application.

- Patch reconstruction: **passed** from the fresh pinned checkout; no literal patch markers or malformed preprocessor lines remain in the reconstructed C++.
- Differentiating preflight regression on the pre-change patched tree: **expected failure, rc=1**, at `Android maintenance4 must follow physical support while retaining the non-Android policy`. This demonstrates that the new check rejects the old unconditional Vulkan 1.3 behavior.
- The same regression on the candidate: **passed, rc=0**, including post-failure exact-feature diagnostics, both temporary-resource fallbacks, conditional aliases, and wrapper policy.
- Surrounding focused checks: **passed** for Vulkan wrapper policy, actual-function unified-memory selection (160,000 four-type configurations and 2,560,000 exact mask lookups), BC/DXT fallback, MSAA depth resolve, Present path (46 structural checks), frame pacing (25), FXAA, GPU/API negotiation, and driver-recovery markers.
- Fresh broad host regression: **62/62 commands across 11 groups in 104.763939 seconds**, zero failures, zero timeouts, and zero resumed commands. This includes the current native core, DXT, fake/real XAudio2 modes, mods/plugins/scripts, diagnostics/settings, patched-DXVK policy, real cache, freshly rebuilt Bink at 32,000/44,100 Hz, real-DLL differential, and reproducibility controls.
- Independent manifest/raw-stream verification: **442 checks passed**, including every return code/timeout/resume field and all 124 stdout/stderr byte counts and SHA-256 values. Manifest SHA-256: `d03d8ed7fdef01652cb93cd60b69c230e707568245f564a3371e90fbc5cfc1db`.
- Exact production commit: `e1277dbc6448c27c10d1ee58070b1eefeb10da85`, pushed to `arena/5eb1c06c-protodroid`. Its three committed source blobs match the pre-push validation hashes.
- Exact `android-deps` CI run [`41927692571`](https://github.com/Nazatric/ProtoDroid/actions/runs/41927692571): **success**, including ARM64 and x86_64 dependency builds with the patched Android DXVK source.
- Exact `build-apk` CI run [`41927692628`](https://github.com/Nazatric/ProtoDroid/actions/runs/41927692628): **success**. The DXVK policy test, host/Bink tests, ARM64 graphics tests, source ARM64 `libmain.so` and plugin/Lua builds, APK packaging, and `Verify APK and embedded Android DXVK` all passed. Protected signing and the release job were skipped; disposable CI signing was used.
- CI metadata verification: **52 checks passed** over both exact-head runs, every job/conclusion, required package/signing/verification steps, source scope, and artifact metadata. Artifact `13372401474` (`prototype-apk`) is server-described as **57,955,117 bytes**, archive digest `sha256:013487b3b1a63b45e93b63f8b8d03fa10a4cf94716d9ff938b401f96ba2cfbb3`. This is artifact metadata, not an inner-APK hash, installation result, or device test.

Raw evidence, reconstructed before/after DXVK sources, source hashes, runner copy, per-command streams, and verification JSON are under `/home/user/protodroid-016-evidence/issue-32-maintenance4/`; the reconstruction roots used for the runs were `/tmp/protodroid-dxvk-maint4-before` and `/tmp/protodroid-dxvk-maint4-after`. Large/generated evidence is not added to Git.

## 5. Correctness scope and performance disposition

The correction changes only an optional capability request and its matching probe gate. Required features—including robust buffer access, multi-draw indirect, geometry/image-cube/depth/fill/sample features, shader draw parameters, mirror clamp, Vulkan memory model, host query reset, timeline semaphores, demote, synchronization2, dynamic rendering, and the explicit Vulkan 1.2 extended-dynamic-state path—remain required. BC/DXT, null-descriptor, robustness, memory-type, Bink, audio, mods, controls, dynamic shadows, AA, texture-quality, FIFO/Reset, and package/version behavior are untouched.

No FPS, frame-time, shader-loading, memory-allocation-rate, sustained/thermal, or power before/after result is claimed. On a device lacking maintenance4, the fallback can create temporary resources during memory-requirement queries; this is a correctness/compatibility tradeoff, not an optimization. Devices with maintenance4 retain the direct query path.

## 6. Limitations and issue disposition

There is no local Android SDK/NDK, Java, Bionic execution, ADB device, GPU profiler, or physical PurpleVK/Adreno 610 run in this environment. Host tests and source reconstruction alone do not prove phone rendering. Exact CI has now compiled the complete patched dependencies and source ARM64/APK package and passed its shader/Java/ARM64/embedded-DXVK gates, but that still supplies no physical-device rendering, audio, performance, installation, or sustained-play evidence.

**#32 remains open.** The current report does not reveal whether maintenance4, another requested feature, or a driver/feature-query inconsistency caused its old failure. A current affected-version run must retain the complete DXVK log. If device creation still returns `VK_ERROR_FEATURE_NOT_PRESENT`, the post-failure requested-feature audit should name the remaining unsupported request; that result must drive any further action.

## 7. Next priority

The focused source/test follow-up and both exact CI runs are complete; keep the release boundary unchanged. Do not bump the version, merge, tag, release, or advertise an issue fix. The next useful evidence is a current #32 reproduction (or another Android Vulkan 1.3 device demonstrably lacking maintenance4) using the exact candidate APK, with full logs and rendering/loading validation. If the failure persists, preserve the post-failure requested-feature audit rather than guessing another feature. Other unresolved reports retain their previous dispositions.
