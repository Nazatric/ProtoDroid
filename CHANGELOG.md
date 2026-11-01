# Changelog

## 0.15

- Added default helicopter **DESCEND** (`V`), an independent numbered **123** touch pad, and **ABC** Android keyboard input through SDL. Existing custom-layout indices and Extra buttons behavior are preserved. Added optional fixed Landscape/Reverse recovery while keeping Auto orientation by default.
- Made High retain full 32-bit decoded DXT1 color; Low/Medium and the standalone default keep the lower-bandwidth 16-bit path. Added a persistent **VERSIONS** action for official older APKs while exact-release updates and verified download recovery remain separate.
- Corrected the Vulkan 1.1 compatibility wrapper’s DXVK-facing effective API advertisement without lowering DXVK’s requirements or bypassing its isolated feature/extension probe. Physical Mali, PowerVR, long-session, and visual outcomes remain unverified.
- Simplified the in-game gear to live FPS/battery/menu pacing, touch controls, FPS overlay, and Layout. Restart-only graphics and engine/mod experiments remain in the launcher, where concise explanations appear on touch, hover, or focus. Menu-FPS changes now use atomic live state consumed when Bink movies open.
- Reworked Mods as a library-only screen with author-defined per-mod toggles, stepped sliders, choices, and text. Added atomic option saves, dirty-back/reset handling, large bounded texture-project imports, shallow library scans, and lazy per-mip DDS reads. Original format/dimension limits remain; arbitrary texture resizing is not claimed.
- Added native plugin ABI v2 with per-mod boolean/number/string getters while retaining the exact ABI-v1 host contract. Updated the source-built example to use real saved options. Windows x86 ASI/DLL compatibility is still not claimed.
- Kept launcher artwork visible from the first intro frame, added transient immersive system-bar behavior to launcher/mod screens, and retained reduced-motion support. Isolated the Android 11+ insets controller from older launcher startup and added a non-fatal legacy fallback. Available-update **DOWNLOAD** now opens the fetched GitHub release directly; existing in-app downloads retain verified recovery.
- Reduced duplicate diagnostic-tour sync/logging without shortening its 22-phase plan or compressing input timing. Added exact action counts, wall/process-CPU time, gameplay dispatches, Presented frames, and rolling timing snapshots. Durable failures, shadow phases, isolated saves/logs/cache, and non-visual evidence boundaries remain.
- Shortened GitHub issue forms and removed decorative emoji. Fixed release signing parity across dispatch types and separated persistent-certificate-change approval from disposable-release approval. The 0.14 disposable private key is unrecoverable, so a differently signed 0.15 requires one uninstall; protected-secret configuration remains an administrator step.
- Host/source validation covers the new policies and native paths. Physical-device visuals, large-pack performance, mod behavior, tour outcomes, and update installation remain unverified until tested on hardware.

## 0.14 - 2026-11-01

- Added an optional local mod platform with staged ZIP/single-file imports, prioritized read-only file overlays, same-format DDS replacement/dumping, and a source-built AArch64 plugin ABI v1. The Prototype-style Mods library includes covers, descriptions, installed/active state, background imports and lifecycle operations, next-Play enable/disable, confirmed removal, and native-plugin trust confirmation. Unchanged Windows x86 ASI/DLL plugins are not compatible; physical mod behavior remains unverified.
- Expanded the independent Prototype Fix equivalents: finite fractional FOV offsets, bounded 512–8192 shadow-map scaling, additional existing-limiter FPS choices, and launcher controls for tick/QTE policy, loose files, developer menu, VRAM reporting, and texture dumping. Legacy archives with one safe top-level wrapper now retain their texture/plugin/overlay layout. Defaults, dynamic-shadow policy, and the required `proto_uv.dat` validation are unchanged.
- Reworked Setup, Mods, and Drivers into a shared Prototype artwork/font/cut-panel language with reduced-motion support, press/focus feedback, keyboard/D-pad navigation, and consistent transitions. Engine INI updates now use synchronized temporary-file replacement and show a visible error instead of silently leaving stale or truncated configuration.
- Fixed Dobby's stale CPU-header integration and cross-ABI assembly preprocessing, and retained exact ARM64 plugin/native linkage checks. No universal rendering, black-screen, or FPS fix is claimed from host/CI evidence.
- Added an Auto-only Vulkan 1.1 compatibility candidate built from pinned GameNative/Mesa source with Android WSI, isolated capability probes, and direct DXVK ICD handoff. Direct 1.2/1.3 and explicit Turnip/PanVK choices remain in place; physical Mali validation is still required.
- Added the deliberate **MERCER**/seven-logo-tap diagnostic tour with ordered presented-frame gameplay phases, isolated saves/logs/cache, durable phase/crash markers, and detailed diagnostics. Scripted completion is not visual verification.
- Fixed the tour's missed `InGameState` entry probe by recognizing the engine's recompiled vtable thunks, expanded the plan to 22 phases with targeting/grab/wheel/form inputs, and required gameplay-task activity during every executed phase. Isolated tours now close at the engine-loop boundary instead of repeating an observed `EngineTerminate` hang, with watchdog-backed failure exit after backtraces if the engine cannot reach that boundary; ordinary Play retains its normal shutdown and report-only watchdog paths. Physical tour results still require device validation.
- Fixed diagnostic-tour timing when several Presents coalesce before one engine update: logical action frames no longer compress press/release pairs into that single boundary. Phases now record actual Present dwell separately from action frames, while the launcher and durable result clarify that closing the isolated tour process is expected. Two uploaded Mali-G720 tours completed without a recorded native crash; remaining long-frame causes and any separate impact/freeze are not claimed fixed.
- Redesigned **Drivers** around a clear current choice, large plain-language mode cards, 48 dp minimum touch targets, and an optional Driver packages section with Installed/Downloads tabs, family-aware filtering, progressive details, and confirmed removal. Pinned downloads, validation, explicit Custom selection, recovery, and native driver policy are unchanged.
- Applied DXVK's official Prototype workaround explicitly because the native Android host is named `app_process64`, not `prototypef.exe`, so DXVK's built-in game profile did not match. Disabling the broken DF-format exposure targets the original dynamic-shadow and bloom paths without reducing shadow detail, resolution, textures, AA, or render scale. Physical Adreno/Mali visual confirmation is still required.
- Made launcher downloads resilient to transient empty DownloadManager queries and exceptions: recovery now uses timed rechecks, preserves uncertain downloads, and only replaces confirmed failed or persistently missing rows. Verification retains APK hash, package/version, and signing-certificate checks.
- Added **GO TO RELEASES** beside updater recovery, with Retry preserved. Launcher resume checks now run after a 15-minute backoff without hiding an existing update card or interrupting active downloads. Kept FIFO presentation and known-pipeline cache warm-up intact; no universal 60 FPS or exhaustive future-shader precompilation claim is made.

## 0.13 - 2026-10-31

- Fixed the Drivers screen's **BACK** crash: touch tracking is now released before closing the screen can detach its view, with null-safe cleanup shared by touch cancellation and detachment. Both the on-screen BACK action and Android system Back return to the existing launcher homepage instead of terminating it. Added entry/exit and cleanup-order regression coverage; gameplay and native driver behavior are unchanged.
- Brings the validated 0.10–0.12 driver updates into this release: the dedicated driver center and verified custom packages, Adreno 825 Turnip selection, Qualcomm unified-memory correction, and automatic startup recovery. Device-specific compatibility still requires physical testing; see the sections below for full details.

## 0.12 - 2026-10-31

- Fixed the **Drivers** button crash: opening the dedicated screen detaches the launcher view during touch-up, so touch cleanup now retains and null-checks the original parent instead of dereferencing a removed view. Added a regression check for this content-view replacement order; driver rendering, downloads, and game/native behavior are unchanged.
- Consolidated the open Adreno reports without another version bump: the Adreno 710/750 logs confirm the same 90,808-byte unified-memory failure already backported from upstream DXVK. Expanded the exact regression coverage and made dependency builds reject a missing patch; physical #15/#17/#18 outcomes still require device validation.
- Improved Auto recovery for clean D3D9 initialization failures such as #16: an unconfirmed selected driver is recorded so the next launch can try System, with a clear recovery message. Explicit Turnip/Custom choices still retry by choice, and the A825 Gen8 policy is unchanged.

## 0.11 - 2026-10-31

- Replaced the hidden driver-package row and generic dialogs with a dedicated Prototype-styled **Drivers** screen, accessible from the launcher and a fixed Settings button. Auto remains the default; the screen explains Auto/System/Turnip/Custom and Mali fallback behavior.
- Added a scrollable/filterable multi-package library with explicit Select and two-tap Remove, private legacy migration, hash-deduplicated imports, and in-screen download/progress/error states. Added pinned GitHub downloads for standard, One UI, and Gen8 Turnip plus PanVK-kbase; installing a package never silently selects Custom.
- Added direct Vulkan ICD loading for explicitly selected PanVK-kbase packages while retaining AdrenoTools for Turnip. Probe and gameplay share backend selection, mapped-library checks, and crash recovery; Turnip is refused on non-Adreno devices. Exact BC texture behavior and accepted rendering quality are preserved.
- Expanded host/multi-slot tests and added CI validation of every pinned release asset and PanVK's ICD export. Physical Mali PanVK gameplay and device-specific black-screen outcomes remain unverified; no universal compatibility, FPS, or black-screen fix is claimed.

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
