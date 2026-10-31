# Changelog

## 0.12 - 2026-10-31

- Fixed the **Drivers** button crash: opening the dedicated screen detaches the launcher view during touch-up, so touch cleanup now retains and null-checks the original parent instead of dereferencing a removed view. Added a regression check for this content-view replacement order; driver rendering, downloads, and game/native behavior are unchanged.

## 0.11 - 2026-10-31

- Replaced the hidden driver-package row and generic dialogs with a dedicated Prototype-styled **Drivers** screen, accessible from the launcher and a fixed Settings button. Auto remains the default; the screen explains Auto/System/Turnip/Custom and Mali fallback behavior.
- Added a scrollable/filterable multi-package library with explicit Select and two-tap Remove, private legacy migration, hash-deduplicated imports, and in-screen download/progress/error states. Added pinned GitHub downloads for standard, One UI, and Gen8 Turnip plus PanVK-kbase; installing a package never silently selects Custom.
- Added direct Vulkan ICD loading for explicitly selected PanVK-kbase packages while retaining AdrenoTools for Turnip. Probe and gameplay share backend selection, mapped-library checks, and crash recovery; Turnip is refused on non-Adreno devices. Exact BC texture behavior and accepted rendering quality are preserved.
- Expanded host/multi-slot tests and added CI validation of every pinned release asset and PanVK's ICD export. Physical Mali PanVK gameplay and the logless S24 Ultra/0.9 black-screen report remain unverified; no universal compatibility, FPS, or black-screen fix is claimed.

## 0.10 - 2026-10-31

- Added a pinned A825 Gen8 Turnip build and made Auto select it for Adreno 825/SM8735 instead of skipping directly to Qualcomm's driver. Other Adreno 8xx models retain their previous automatic policy.
- Added **Auto / System / Turnip / Custom** driver settings and a **Driver packages** manager with ZIP/raw-SO import, removal, and hash-pinned in-app Turnip Gen8 and experimental PanVK-kbase downloads. PanVK uses exact BC modes, not its optional lossy texture paths.
- Added staged ELF/metadata/checksum validation, rollback-safe replacement, legacy import migration, isolated custom-driver probes, and mapped-library checks against silent stock fallback. Missing custom packages restore Auto; System remains available for recovery.
- Backported DXVK's upstream unified-memory type-selection fix for Qualcomm's driver and clarified no-compatible-GPU failures. Rendering and texture quality are unchanged.
- Wired the required Gen8 driver and probe into exact-commit ARM64 APK builds and added host package/policy tests. Signed-APK checks now require the exact source revision and linked driver-selection code. Physical A825 and Mali gameplay still require device validation; no universal compatibility or FPS claim is made.

## 0.9 - 2026-10-31

- Added a launcher update card for published GitHub releases, with versionCode checks, resumable system downloads, APK/certificate verification, progress, and signer-aware installation. Differently signed updates explain the required uninstall first; shared game saves remain in `/storage/emulated/0/Prototype`, and the downloaded APK stays in Downloads for Android's process-change fallback.
- Made detailed engine, Present, texture, and DXVK pipeline diagnostics opt-in. Normal gameplay skips their timing and sampling work while ordinary FPS tracking, ADPF, and shared log/cache mirroring remain active.
- Kept detailed diagnostics and per-thread import profiling available for matched stall investigations without enabling them for every player.
- Added the exact source revision to native startup logs and APK provenance metadata. Release verification now requires it to match the commit being built.
- Corrected texture diagnostics to count successful creations and label cumulative top-level estimates as non-resident; decoding and texture quality are unchanged.
- No graphics, Bink, control, save, presentation, or cache-behavior changes; no FPS improvement is claimed without matched device runs.

## 0.8 - 2026-10-31

- Grouped launcher Settings into **Graphics**, **Game Video**, **Controls**, and **Interface** while preserving every existing option.
- Added functional launcher access to the game's own **Anti-Alias Level**, **Shadow Detail**, and **Graphic Quality** settings through GameDB and its original render application path.
- Added **Game default** choices to preserve device-dependent engine defaults until an explicit level is selected. All settings continue to share the same preferences and `protodroid.ini` persistence.
- Expanded **Low**, **Medium**, and **High** presets to cover the game's AA, shadow, and quality levels as well as the existing port graphics options. FPS and control choices remain independent.
- Moved active shared saves into `/storage/emulated/0/Prototype/Saves/`, with one-time no-overwrite migration from the previous shared root and app-specific layouts. External save management and deletion behavior are preserved.
- Preserved explicit FPS-cap pacing and moved recurring shared cache/log copies off the engine thread while retaining synchronous shutdown flushing. Presets and individual graphics edits now leave unrelated engine settings unchanged; diagnostics separate Present() wall time from frame pacing.
- Added timestamped five-second diagnostics for frame stalls, engine work, pipeline calls, file I/O, CPU threads, memory, ADPF, and supported thermal/headroom APIs, with a whole-run summary and no shader-dump overhead.
- Stopped camera-only touch movement from redrawing the entire control overlay. Mouse motion, stick/wheel feedback, editing, and all existing controls are unchanged.
- Allowed the engine's worker sizing to use the logical CPUs actually visible to Android instead of a fixed four-core mask, without pinning threads or changing graphics.
- Made restricted or zeroed device CPU counters report **unavailable** rather than a misleading 0% reading.
- Added protected persistent-release signing configuration, certificate-digest reporting, and native-build provenance checks. Releases reject ephemeral keys by default and always reject dummy/prebuilt native inputs; differently signed installations require a one-time uninstall.

## 0.7 - 2026-10-30

- Turned the setup screen into a launcher: **Settings** and **Play** replace the permission actions after access is granted. Game detection remains automatic, while Play starts the game deliberately and rechecks for newly copied files.
- Added a matching settings page with scrolling, full touch feedback, and **Low**, **Medium**, **High**, and **Custom** graphics presets.
- Included every existing graphics, performance, and touch-control option. Presets change only resolution, textures, and anisotropy, leaving FPS and controls untouched.
- Unified the launcher and in-game settings through the same saved preferences and `protodroid.ini` values. Launcher changes save automatically and engine options apply on Play.

## 0.6 - 2026-10-30

- Rebuilt the first-launch setup screen with the supplied Prototype artwork, icon, Barlow Condensed fonts, status displays, and animations.
- Kept automatic game detection and launch, all three supported game directories, permission refreshes, and custom-driver installation intact.
- Made the full **Allow file access** label fit instead of cutting it off on narrower screens.
- Clarified the app-specific storage instructions: Android 11 or above uses PC/USB, while Android 10 or below can use a file manager with storage access.
- Removed obsolete device logs, superseded design archives, and the uploaded cache seed from the repository. No cache seed is shipped with the APK.

## 0.5 - 2026-10-29

- Moved shared profiles and saves to `/storage/emulated/0/Prototype/` when all-files access is available.
- Made external save creation, replacement, and deletion work through the same case-insensitive path mapping used by the game.
- Added a one-time, no-overwrite migration from both legacy app-specific save layouts. Legacy files remain as backups and deleted shared saves do not return.
- Kept the original app-specific save directory as a fallback when shared storage is unavailable.

## 0.4 - 2026-10-29

- Packed native skinning matrix and bounding-box arithmetic into four-lane operations without changing the original arithmetic association.
- Added direct DXT1-to-16-bit row writes and exact RGB565 expansion tables while preserving decoding and transparency behavior.
- Expanded native ARM64 differential testing to 2,000 randomized skinning batches and exhaustive DXT1 endpoint pairs.

## 0.3 - 2026-10-29

- Added a direct DXT1-to-A1R5G5B5 decode path for Mali uploads, avoiding an intermediate per-pixel narrowing pass.
- Made shared shader-cache backups atomic so an interrupted backup preserves the previous usable copy.
- Added texture decode, creation, and upload timing counters to help correlate device stalls with the work that causes them.

## 0.2 - 2026-10-29

- Restored the proven `-O2` engine compilation and DXVK swapchain image-count behavior after unproven optimization experiments.
- Preserved the confirmed fullscreen path, graphics quality, cache recovery, FIFO/Reset behavior, Bink playback, audio, and mouse controls.
- Added the optional **DSG PWR**, **WEB**, **DISGUISE**, and **ALEX** touch buttons. Extra buttons remain Off by default.
- Kept existing control-layout indices and the centered Settings header unchanged.
