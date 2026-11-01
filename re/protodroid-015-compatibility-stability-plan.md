# ProtoDroid 0.15 compatibility and stability plan

Status: research completed before edits; the planned source changes and local host/source validation are now complete. Exact-commit ARM64 CI is pending. This is not physical-device validation or a release approval. Version 0.15 remains unmerged and unreleased.

## Method and boundaries

The investigation follows `aicodeking/leanrea`: work on copies, inventory first, use targeted probes, and distinguish **seen**, **inferred**, and **unknown**. Original game files and `TestingFiles` remain read-only. Research copies are under `/tmp/protodroid-deep-re`, `/tmp/protodroid-sdl-research`, and `/tmp/protodroid-mesa-wrapper-research`.

Inspected sources include the complete GitHub issue bodies/comments for #21, #22, #23, and #25–#29; their native/DXVK attachments; SDL 2.30.9; DXVK 2.5.3; GameNative/Mesa wrapper commit `6bcfbaa701a8d1efa7f6c54145495eda5329f767`; Android's Vulkan pre-rotation and ADPF documentation; Prototype's PC control references; and the current Java/native/update/driver code.

Preserve the mod platform, ARM64 ABI, original files, saves, controls/layout indices, safe defaults, dynamic shadows, Bink audio/video, FIFO/Reset behavior, default-On `Unlock menu FPS`, driver recovery, and existing graphics/audio behavior. Do not fabricate `proto_uv.dat`, bypass its validation, hide the issue-#21 null dereference, relabel Vulkan capabilities, or claim a device fix without a device trace.

## Evidence inventory

### Seen

- **#21 / Helio G96 / PanVK:** the supplied run reaches frame 64, then faults at guest address `0x1000000004` in `f_7c486bf0+0x64`. Targeted DLL inspection connects this to the required `proto_uv.dat` front-end path. Current source already rejects missing/unreadable required UV data. A null-manager guard would only move the failure.
- **#22 / Mali-G57:** the older log reaches Bink/menu operation and later stalls; it does not establish one rendering, shader, cache, or threading root cause. The log also shows CPU DXT decoding and reduced 16-bit DXT1 output.
- **#23 / Poco F6 / Adreno 735:** DXVK creates the device and swapchain, then fails a 90,808-byte allocation with memory types `0,6`; native loop count stays zero. Current source already contains the exact upstream unified-memory type-selection backport for this log pattern. No second memory rewrite is justified.
- **#25 / PowerVR BXM-8-256 / Vulkan 1.1:** explicit System correctly rejects the direct path. Auto loads the wrapper in its isolated probe, but the effective device is still advertised as 1.1 and is rejected before feature validation. Mesa's `wrapper_effective_api_version()` raises the version only for an explicit version override; ProtoDroid currently does not set `WRAPPER_VK_VERSION`. The wrapper has actual lowering code, and the probe separately checks DXVK's required effective features/extensions.
- **#26:** the reported camera offset matches the old +20-degree default; current source defaults to Original. Touch controls were hidden behind a menu, and low/high power impressions have no trace that identifies a common cause.
- **#27 / Helio G200 Ultra / PanVK:** no log was supplied. A PanVK-specific root cause is unknown.
- **#28 / Mali-G57 MC2 / Vulkan 1.3:** native and DXVK logs show successful device creation, FIFO reset, Bink playback, and continued frames. This is not a simple device-creation failure. The run uses CPU DXT decoding with DXT1 narrowed to A1R5G5B5. The current High preset does not override that hidden reduction.
- **#29 / Mali-G57 / Vulkan 1.1:** only a screenshot is supplied; the same honest wrapper negotiation/feature-probe path applies, but device support is not established.
- **Controls:** `TouchControls.java` sends real SDL key/mouse input. Number keys 1–4 exist only in the optional Extra-buttons group, and no `V` helicopter-descend button exists. SDL 2.30.9 provides `SDLActivity.showTextInput(...)`; its IME connection converts ASCII input to SDL key events as well as text events, so the existing engine keyboard service can be used without inventing a second text API.
- **Releases:** official `prototype.apk` assets remain published for 0.2–0.8, 0.13, and 0.14. The launcher's archive action can instead open the latest/exact update page, and is only prominent during update recovery.
- **Orientation:** activities use `sensorLandscape`; surface destruction/readiness is guarded. DXVK requests identity pre-transform and lets Android's compositor rotate, which is a valid Android path. Changing it blindly to `currentTransform` without rotating rendered content could double-rotate output.
- **CPU/RAM/power:** the engine is already offered all available low Windows CPU indices, host affinity is not restricted, ARM compiler workers are capped at two, ADPF and thermal diagnostics exist, FIFO remains enabled, and Battery saver can impose 30 FPS. There is no evidence for a new scheduler, arena, or thread-pool rewrite.

### Inferred implementation candidates

- Setting the wrapper's effective version to 1.3 **while retaining the isolated effective-feature probe** corrects the observed negotiation omission. It may expose further genuine missing features on PowerVR/Mali; those must still fail closed.
- A default-visible `V` control, an explicit compact 1–4 pad, and an SDL soft-keyboard action address the reported input gaps without changing saved layouts or the engine's key mapping.
- Exposing the existing `decode_16bit` choice and selecting 32-bit DXT1 output for High preserves lower-preset bandwidth options while giving High a full-color path. This is a quality correction and workaround, not proof that it resolves every #22/#28 visual defect.
- A persistent link to the official release archive is the safest rollback mechanism. It avoids third-party APK mirrors and preserves the existing verified update/recovery flow.
- An optional fixed Landscape/Reverse-landscape choice is a bounded recovery workaround for devices whose orientation selection is wrong. Auto must remain the default; this is not presented as a swapchain root-cause fix.

### Unknown / not a basis for speculative edits

- The S23 Ultra long-session crash/flicker/upside-down reports have no matching native tombstone, DXVK trace, or reproducible transform log.
- The exact #22/#28 transparent-wall/glitch cause, #27 PanVK crash cause, and any universal Adreno/Mali/PowerVR shader workaround.
- Whether the corrected wrapper passes all required features and renders correctly on affected Vulkan 1.1 devices.
- Whether any settings change lowers power on a particular phone. Frame cap, FIFO, resolution, texture, shadow, and thermal behavior must be measured on-device.

## Implementation batches

### 1. Vulkan compatibility negotiation

- Set Mesa's wrapper effective-version override only around the wrapper probe/runtime configuration.
- Keep base Vulkan 1.1 as the minimum, preserve direct 1.2/1.3 and explicit driver choices, and retain all required feature/extension checks after the version query.
- Add a source-policy regression test proving that probe and runtime configure the override and that direct paths do not receive it.
- Report the result as an improved compatibility candidate, never as guaranteed Vulkan 1.1 support.

### 2. Touch and keyboard controls

- Append a default-visible `DESCEND` button mapped to `V`; do not reorder existing buttons or saved layout indices.
- Make the existing 1–4 inputs explicitly numbered and available through a compact in-game `123` toggle, independently of the larger Extra-buttons group. Release held inputs when changing pads.
- Add an `ABC` action using SDL 2.30.9's real text-input view/IME path. Keep normal hardware/touch key handling and Android Back behavior; do not fabricate engine text-field detection or claim non-ASCII/engine-widget awareness.
- Add source tests for the key mapping, default visibility/group behavior, SDL entry point, and append-only layout contract.

### 3. Graphics quality and orientation recovery

- Add a next-Play DXT1 color setting backed by the existing native `decode_16bit` option. Keep the established reduced default outside presets; select full 32-bit color at High and retain reduced output at Low/Medium unless the user customizes it.
- Document the advanced INI choice and its memory/bandwidth tradeoff. Preserve BC decoding, mip behavior, shadows, AA sample handling, and mod texture keys.
- Add Auto/Landscape/Reverse-landscape recovery choices, applied before the game activity starts. Keep Auto/`sensorLandscape` as the default and do not alter DXVK pre-transform on speculation.

### 4. Rollback access and documentation

- Add a persistent launcher action for the official GitHub release archive, independent of the latest-update destination and error state.
- Preserve direct exact-release update behavior and existing APK hash/package/certificate verification for in-app downloads.
- Document that downgrades/signature changes can require uninstall, shared saves remain separate, and app-private data should be backed up. Link only the official archive.

### 5. Validation and evidence updates

- Run Python source/policy tests, Java host tests available in CI, native UBSan suites, graphics/DXVK checks, mod-platform tests, and diff hygiene.
- Push the batched candidate and require the complete exact-commit workflow: host tests, dependency checks, real ARM64 native/plugin/wrapper linkage, Android compilation, driver/graphics/save/tour tests, signed APK packaging, and artifact/source verification.
- Do not merge or release 0.15. Do not claim physical-device confirmation without affected-device logs/traces; `adb` is unavailable in this environment.
- Record any unresolved device-specific items and the exact CI result in the research documentation. Keep the README concise, English-only, and free of universal compatibility/FPS claims.
