# Changelog

## 0.8 - 2026-10-30

- Grouped launcher Settings into **Graphics**, **Game Video**, **Controls**, and **Interface** while preserving every existing option.
- Added functional launcher access to the game's own **Anti-Alias Level**, **Shadow Detail**, and **Graphic Quality** settings through GameDB and its original render application path.
- Added **Game default** choices to preserve device-dependent engine defaults until an explicit level is selected. All settings continue to share the same preferences and `protodroid.ini` persistence.
- Expanded **Low**, **Medium**, and **High** presets to cover the game's AA, shadow, and quality levels as well as the existing port graphics options. FPS and control choices remain independent.
- Moved active shared saves into `/storage/emulated/0/Prototype/Saves/`, with one-time no-overwrite migration from the previous shared root and app-specific layouts. External save management and deletion behavior are preserved.
- Preserved explicit FPS-cap pacing and moved recurring shared cache/log copies off the engine thread while retaining synchronous shutdown flushing. Presets and individual graphics edits now leave unrelated engine settings unchanged; diagnostics separate Present() wall time from frame pacing.
- Stopped camera-only touch movement from redrawing the entire control overlay. Mouse motion, stick/wheel feedback, editing, and all existing controls are unchanged.

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
