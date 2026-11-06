# Post-Phase-14 follow-up: explicit Vulkan wrapper recovery

Status: **source/host launcher-recovery correction on the working branch; issue #33 remains open and unconfirmed**. Production commits `e3122267ca8f41ef845abfd58eaeb45c28ae0e3f` (explicit recovery) and `99db3e8b2d9fefab8fe62526b086f6ba834fe5a1` (native marker directory) follow documentation head `a7b8873cfbff5a8e8f8643f349e3bf7e7505c4f3`. This is a follow-up to the closed Phase 14 audit, not a new timed phase, a performance claim, or a release-readiness change.

## 1. Report and diagnostic boundary

Issue #33 reports a Motorola Edge 2021 5G UW (Snapdragon 778G, Adreno 642L) returning to the home screen after Play. The two attached logs are historical: both identify source `ea2ba6c9f1e9c9114bcf37f1bcc796071deab4eb`, an October 31 build, and `vulkan_driver = custom`. The imported `vulkan.purple.so` falls back to the Adreno 642L system Vulkan 1.1.128 driver; that revision explicitly rejects the direct path because it requires Vulkan 1.2 with its required extensions, or 1.3. The logs do not contain a native fatal-crash trace.

The later comments—"Same issue with the latest version it kicks me to homescreen" and "Still the same issue"—remain current as of this investigation, but supply neither an exact tested revision nor fresh native/DXVK/wrapper logs. The old Vulkan 1.1 diagnosis therefore must not be carried forward as proof of the latest recurrence.

The old configuration is also relevant to test interpretation: the compatibility wrapper remains an **Auto-only** fallback. A failed explicit Custom selection retains its documented System fallback; this follow-up does not silently reroute Custom into the wrapper.

## 2. Vulkan 1.1 negotiation audit

The audit used the production `src/native/turnip_load.h` and the exact pinned GameNative/Mesa source `6bcfbaa701a8d1efa7f6c54145495eda5329f767` used by `android/build-vulkan-wrapper.sh`.

The source confirms the intended sequence: the wrapper advertises an effective DXVK interface, receives the DXVK/1.3 application request, lowers the real base-loader request to its supported version, and exposes a lowered physical-device feature set. ProtoDroid separately queries that effective feature set instead of inferring support from the base API number. The base-loader copy used for `vkCreateInstance` does not overwrite the original application information retained by Mesa for `wrapper_core13_wanted`.

A temporary C++ harness executed the **actual production probe header** against mocked Vulkan entry points under ASan/UBSan. **26/26 checks passed**, covering:

- Vulkan 1.0 wrapper-base rejection before instance creation;
- a Vulkan 1.1 loader with an effective 1.3 device, including the exact 1.3 request and `DXVK` engine identity;
- the effective Vulkan 1.2 extension-equivalent route;
- acceptance with `maintenance4=0`, preserving the preceding capability correction;
- rejection and exact reporting of missing dynamic rendering, synchronization2, swapchain, and the 1.2 route's required extensions;
- rejection of an unraised 1.1 physical device and missing `features2` entry points;
- instance-creation failure reporting and temporary-instance cleanup on success and feature rejection;
- unchanged direct-driver 1.1 rejection.

This rules out the modeled probe/negotiation cases, not the real Adreno driver or the complete Mesa wrapper. The mock supplies intended answers; it is not physical-device rendering or startup proof.

## 3. Source-provable recovery defect

A separate, concrete defect exists in the recovery path:

- `TryVulkanWrapper` reads the same-build `vulkan_wrapper_failed` marker before every automatic wrapper attempt. This is intentional crash-loop protection after an in-process startup failure.
- The native source deliberately does not unlink that marker on ordinary launches.
- The launcher's only cleanup method attempted to delete `turnip_failed`, not `vulkan_wrapper_failed`.
- That cleanup used `activity.getFilesDir()`, Android's internal files directory. Native instead calls `chdir(SDL_AndroidGetExternalStoragePath())` before writing the markers, so even the existing Turnip cleanup targeted the wrong directory.
- Explicitly choosing Automatic/System/Turnip/Custom in the Drivers screen did not invoke even that incomplete cleanup.

Consequently, one same-build wrapper startup failure could disable every later Auto wrapper attempt with no normal launcher action capable of retrying it. The practical recovery was a manual deletion in the external app-specific directory, app-data reset, or a different native build. This is a plausible persistence mechanism for a recurring home-screen failure, but the available #33 evidence does not prove that it caused the reporter's original or latest failure.

## 4. Exact change

- `android/java/com/protodroid/DriverManager.java`: the existing explicit driver-action cleanup is now package-visible as `clearFailureMarkers()` and deletes both `turnip_failed` and `vulkan_wrapper_failed` from `activity.getExternalFilesDir(null)`, matching native's SDL external-storage working directory. It returns without redirecting cleanup if that directory is unavailable. Existing custom select/install/remove actions use the same cleanup path.
- `android/java/com/protodroid/DriverView.java`: an explicit driver-mode action invokes that cleanup before saving the selected mode. The recovery card now explains that reselecting a driver explicitly retries it and clears its previous startup-failure guard.
- `tools/test_driver_screen_navigation.py`: new differentiating checks require the explicit mode action to invoke cleanup, require both marker names, and bind the Java deletion directory to native's external-storage `chdir` rather than internal `getFilesDir()`.
- `tools/test_vulkan_wrapper_policy.py`: new invariants require ordinary Auto launches to retain `ReadDriverFailureMarker(kWrapperFailed, kBuild)` and forbid a native `unlink(kWrapperFailed)` bypass.

Only explicit user-initiated driver actions clear the guards. Ordinary Auto launches still honor them, so repeated automatic crash loops are not reintroduced. Custom remains Custom, System remains System, and no driver package or setting is silently replaced.

## 5. Raw validation results

- Differentiating regression before the production fix: **expected failure, rc=1**, at `Explicit driver selection does not retry guarded startup paths`. The same regression passed after the explicit-recovery change.
- A second path-binding regression then rejected the intermediate version: **expected failure, rc=1**, at `Recovery deletes markers from internal getFilesDir(), not the native external working directory`. It passed after cleanup was bound to `getExternalFilesDir(null)`.
- Surrounding launcher-policy regression on the intermediate change: **18/18 commands passed in 3.346222 seconds**. One initial ambient-Python run stopped at the issue-template test because `yaml` was unavailable; that failure is retained. Repeating the complete group with the preserved project dependency path produced the clean result, with no test skipped or hidden.
- An initial broad run was stopped after 34 commands when the directory audit changed the worktree mid-run. Its partial output is retained as mixed-worktree evidence and is not counted as final validation.
- Fresh final-worktree broad regression at `99db3e8`: **62/62 commands across 11 groups passed in 108.105506 seconds**, with zero failures, timeouts, or resumed commands. Coverage includes native core, DXT, fake/real XAudio2 modes, mods/plugins/scripts, diagnostics/settings, patched-DXVK policy, real cache, freshly rebuilt Bink at 32,000/44,100 Hz, real-DLL differential, and reproducibility controls.
- Independent manifest/raw-stream verification: **442 checks passed**, including every return code, timeout/resume field, and all 124 stdout/stderr byte counts and SHA-256 values. Manifest SHA-256: `879e836651a45b9c5943fa9ad24b33863e464f50deb75e14eec8cb35b49be053`.
- Native `driver_recovery_test`: **passed under UBSan**, preserving same-build marker behavior and different-build retry semantics.
- Production Vulkan probe harness: **26/26 checks passed under ASan/UBSan**, as detailed above; no production probe edit was needed.
- `git diff --check`: **passed**. The two production commits contain exactly the two Java files and two focused test files described above. Final committed blobs match the validated worktree hashes.

- Exact final `build-apk` CI run [`41936405825`](https://github.com/Nazatric/ProtoDroid/actions/runs/41936405825) on `99db3e8b2d9fefab8fe62526b086f6ba834fe5a1`: **success**. Dependency/Turnip builds, ARM64 graphics tests, driver-package tests, all host/Bink tests, Android Java compilation, source ARM64 `libmain.so` and plugin/Lua builds, APK packaging, and `Verify APK and embedded Android DXVK` passed. Protected signing and the release job were skipped; disposable CI signing was used.
- Independent final CI verification: **53 checks passed** over the exact head/branch, every job and required step, two-commit source scope, protected-release boundary, and artifact metadata.
- Final CI artifact `13375940539` (`prototype-apk`) is server-described as **57,956,440 bytes**, archive digest `sha256:f2cb7b06f60f1f6368a96a4e691ba13e8580d0422d65bd2bbf0c8cb01dc7b543`. It was not downloaded, installed, or device-tested; this is artifact metadata, not an inner-APK hash or phone result.
- Intermediate run `41933243157` on `e3122267` also completed successfully, but it predates the external-directory correction and is **superseded**. Its artifact `13374622276` is not the final candidate and is not delivered as one. Cancellation was attempted before that run completed, but GitHub returned HTTP 403; the completed run and its disposition are retained rather than hidden.

Raw before/after streams, launcher manifests and hashes, native-test output, the mocked probe source/result, issue snapshots, commit metadata, and CI metadata are under `/home/user/protodroid-016-evidence/issue-33-wrapper-recovery/`. Large/generated evidence remains outside Git.

## 6. Correctness and performance scope

The change affects launcher recovery metadata only. It does not alter Vulkan API negotiation, feature requirements, DXVK, Mesa, Turnip/PanVK, BC/DXT decoding, Bink/cutscenes, audio rates or synchronization, mods, controls/layouts, texture quality, AA, dynamic shadows, FIFO/Reset behavior, package identity, saves, or app data. There is no feature spoofing, renderer replacement, hidden quality reduction, or automatic retry-policy relaxation.

No FPS, frame-time, pacing, shader-loading, memory, thermal, power, or sustained-play before/after result is claimed. This is a correctness/recovery change, not an optimization.

## 7. Limitations and issue disposition

There is no local Android SDK/NDK, Java compiler, Bionic execution, ADB device, GPU profiler, or physical Adreno 642L run in this environment. Host mocks and CI compilation cannot establish phone rendering, startup, installation, or sustained-play behavior.

**#33 remains open.** This follow-up fixes the proven missing launcher recovery path; it does not claim to resolve the reporter's original Vulkan failure or unidentified latest recurrence. #22, #29, #32, and #35 also retain their existing open dispositions.

## 8. Next priority

On the exact candidate build, choose **Drivers → Automatic** explicitly (reselecting it now clears an earlier same-build wrapper guard), then Play once and preserve the resulting logs. A useful reproduction needs the exact displayed source revision plus `proto-native.log`, `proto-native.prev.log`, `app_process64_d3d9.log`, and any `vulkan_wrapper.log`, probe log, or wrapper diagnostic file produced. If the wrapper is rejected, its required-feature report should drive the next action; if it reaches DXVK, the DXVK/native failure must be diagnosed instead of repeating the old 1.1 conclusion.

Do not close #33 from this host/CI result. Do not bump the version, merge, tag, or release 0.16 without explicit authorization. The next code change requires a current source/log-provable defect and focused regression coverage.
