# ProtoDroid 0.14 follow-up — implementation plan

Status: planned before the follow-up edits. Original game files and the supplied Prototype Fix archive remain read-only; research is performed on copies. This plan does not claim physical-device validation or unchanged x86 ASI/DLL compatibility.

## Evidence reviewed

### Seen

- The supplied Prototype Fix 1.8b ZIP has SHA-256 `b991747b696497a5d494fec6421da7e8471e773bb8665b24462c337e95e62bf3`. Its ASI is a separate Windows binary; the supplied engine has SHA-256 `d331be205bdde99897952ee88261ab7e63c5c65e36a4660e2a367886a3537071`.
- The complete supplied INI exposes FOV offset, 512–8192 power-of-two shadow maps, custom/window resolution, borderless mode, DInput filtering, tick removal, button-tap timing, FPS override, VRAM reporting, loose files, developer menu, sample-rate handling, CPU-thread and billboard fixes, and verbose logging.
- The [Prototype Fix description](https://www.nexusmods.com/prototype/mods/52) additionally documents the port-27015 launch workaround, AMD shadow workaround, and DPI-scaling behavior. These belong to the Windows launcher/engine environment and must not be copied blindly into Android.
- `aicodeking/leanrea` requires copies, an inventory, targeted probes, and seen/inferred/unknown labels. Previous targeted engine probes and the current source inventory are the basis for this follow-up; no broad speculative decompilation is needed.
- Existing native code already implements controlled CPUID/network/input paths, native audio and fullscreen sizing, optional FOV/tick/QTE/developer-menu/VRAM/loose-file features, overlays, same-format DDS replacement/dumping, and a source-built ARM64 plugin ABI.
- Existing `mod_features.cpp` truncates FOV to an integer and accepts only 1024/2048/4096 shadows, with an internal 4096 clamp. The supplied INI supports fractional FOV and powers of two from 512 through 8192.
- Existing legacy import recognizes root-level texture/plugin layouts. A common archive wrapped in one directory can instead become a loose overlay because generated-content detection does not inspect that wrapper.
- Issue #23's Adreno 735 DXVK log fails a 90,808-byte allocation with memory types `0,6` despite a large heap. The current branch already contains the exact upstream DXVK unified-memory correction for this pattern.
- Issue #21's evidenced null save-manager path is already handled by required `proto_uv.dat` validation. Do not fabricate the file, bypass validation, or hide the null dereference.
- Issue #22's Mali-G57 log reaches Bink/menu operation and later records a no-frame watchdog with waiting threads. It does not identify a safe single rendering, cache, or threading correction.

### Inferred

- A single wrapped legacy content root can be supported safely without relaxing ZIP path, collision, size, ELF, or promotion checks.
- Fractional FOV parsing and the full evidenced shadow range are compatible with the existing detour points. Actual appearance, performance, and driver limits remain device-dependent.
- A shared artwork/font/cut-panel language and reduced-motion-aware transitions will make Mods, Drivers, Settings, and setup feel like one launcher without introducing a UI runtime.

### Unknown / not a basis for edits

- Physical-device high-resolution shadow support/performance, QTE behavior, developer-menu appearance, texture appearance, and third-party plugin behavior.
- A universal fix for every Mali/Adreno black screen, stall, or future shader variant.
- Binary or source compatibility with unchanged Windows ASI/DLL plugins.

## Prototype Fix applicability matrix

| Prototype Fix feature | 0.14 decision |
| --- | --- |
| CPU thread-count crash | Keep controlled native CPUID and existing affinity reporting; do not duplicate the Windows byte patch. |
| Port-27015 launch freeze / billboard download | Keep the native networking behavior, which does not perform the Windows connection path; no speculative billboard rewrite. |
| DInput HID filtering | Keep native keyboard/mouse and separate gamepad input; no Windows HID enumeration. |
| Broken VRAM report | Retain optional DXVK `d3d9.maxAvailableMemory`, default off. Expose conservative values; it is a report, not physical VRAM. |
| AMD shadow workaround | Preserve the accepted Android DXVK Prototype/dynamic-shadow handling. Do not port an unproven x86 patch. |
| DPI scaling | Not applicable to Android's SDL fullscreen surface. |
| Device sample rate | Keep native FAudio/SDL/Bink resampling; no duplicate Windows audio patch. |
| Custom/window resolution and borderless mode | Keep Android fullscreen display/render-height equivalents; no second window mode or EXE patch. |
| Tick-rate removal and button-tap timing | Retain optional generated 15 ms gate policy and gated monotonic QTE adjustment. Defaults remain safe. |
| FPS override | Retain native limiter and default-On `Unlock menu FPS`; add useful higher UI choices without promising a resulting frame rate. FIFO/Reset behavior is unchanged. |
| FOV offset | Parse finite fractional offsets and clamp the resulting FOV as before; expose common offsets in the UI. |
| Shadow map resolution | Support `0`, 512, 1024, 2048, 4096, and 8192; retain ratio-preserving scaling and default off. |
| Loose files / developer menu | Retain guarded optional implementations and expose them clearly. |
| Verbose logging | Keep persistent native/mod diagnostics; no separate Windows log path is needed. |

## Implementation batches

### 1. Native feature and package corrections

- Add a small testable feature-policy helper for supported shadow values and bounded ratio-preserving dimensions.
- Parse `fov_offset` as a finite float, reject malformed input safely, and use the same value for hook installation and the detour.
- Extend shadow validation/clamping through 8192 without changing the default or game Shadow Detail setting.
- Add conservative launcher settings for FOV, shadow resolution, tick/QTE policy, loose files, developer menu, VRAM reporting, and texture dumping. Expand FPS choices only through the existing native limiter.
- Detect one safe legacy archive wrapper when generating a manifest; preserve wrapped texture/plugin/loose layouts and all existing validation.
- Extend host/source tests for these exact behaviors and retain all package, path, ABI, lifecycle, texture, and original-file protections.

### 2. Prototype-style Mods library

- Replace generic platform-form Mods UI with a custom Android canvas view using existing Prototype artwork, icon, Barlow Condensed fonts, red/bone palette, cut panels, and restrained motion.
- Provide installed/active counts, cover art, metadata, content/compatibility/load state, expandable descriptions, enable/disable, two-step removal, native-plugin trust confirmation, and import progress/status.
- Add Mods and Engine fixes views with useful empty states and next-Play messaging.
- Keep text descriptions/content descriptions accessible, touch targets at least 48 dp, scrolling/fling and keyboard navigation functional, and cover decoding bounded.

### 3. Whole-launcher visual and motion pass

- Reuse the same artwork/header/panel language in Drivers and retain the established Setup/Settings functionality.
- Add coherent entry/activity transitions, press/focus feedback, and reduced-motion handling to Setup, Drivers, and Mods.
- Preserve setup detection, all storage paths, Play, Drivers/recovery, updates, diagnostic-tour arming, presets, in-game settings, saves, and default-On `Unlock menu FPS`.
- Keep README English-only, concise, and Prototype-styled; document Mods and the optional fixes without compatibility or performance overclaims.

### 4. Validation and final candidate

- Run host UBSAN native mod tests, new feature-policy tests, Python source/signature tests, and available source checks.
- Require exact-pushed-commit host tests, dependency matrix, real ARM64 native/plugin linkage, existing graphics/Bink/driver/save/tour tests, APK build/sign/verify, and artifact provenance.
- Batch edits before CI; do not produce an APK for every small change and never use dummy/prebuilt native fallbacks.
- Record CI evidence separately from device-dependent behavior. Do not claim universal Vulkan support, stable 60 FPS, complete crash coverage, or physical testing.

### 5. Merge and release

- Open a pull request from `arena/c7fce402-protodroid` only after the newest exact-commit candidate is green.
- Merge that PR, then run the required exact-main build/release dispatch so the published APK is compiled and verified from the released main commit.
- Publish `protodroid-v0.14` with `prototype.apk` and the 0.14 notes. Keep `protodroid-v0.13` and its APK available.
- Supply the actual v0.14 release/APK link, not only an Actions page.
