# ProtoDroid 0.15 — research record

Status: planning baseline, 2026-11-01. Work stays on `arena/78b126d7-protodroid`; 0.15 must remain **unmerged and unreleased**.

## Method and evidence boundary

This pass follows `aicodeking/leanrea`: copy/reference inspection first, inventory second, small targeted probes third. Original game files and `TestingFiles` remain read-only. No Windows DLL/ASI implementation is copied or presented as Android-compatible.

Labels below distinguish **seen** in this checkout or supplied evidence, **inferred** as a design conclusion, and **unknown** without physical-device or protected-secret proof.

## Starting inventory

- In-game UI: `TouchControls.java` currently draws every launcher setting, including restart-only graphics, engine experiments, and mod controls. `GameSettings.java` owns persistence and defaults.
- Launcher: `SetupView.java` owns artwork, transitions, settings, and updater actions. `SetupActivity.java`, `ModsActivity.java`, and `DriverActivity.java` own launcher window behavior.
- Mods: `ModManifest.java`, `ModManager.java`, `ModsView.java`, `ModsActivity.java`, and `src/native/mod_*.cpp` implement schema-1 packages, imports, overlays, DDS replacement, and the ARM64 plugin ABI v1.
- Release/update: `UpdateManager.java`, `UpdateRelease.java`, `android/build-apk.sh`, and `.github/workflows/build-apk.yml` implement verified APK download/install and signing.
- Diagnostics: `diagnostic_tour.cpp`, `diagnostic_tour_plan.h`, `present_stats.*`, `perf_hint.cpp`, and their host tests implement automated gameplay entry and 22 sequential phases.

## Supplied evidence

### Seen — in-game screenshot

`Screenshot_2026-11-01-16-46-29-609_com.protodroid.jpg` shows 24 mixed options in two dense columns. Restart-only graphics, engine experiments, and live touch controls share one panel; the bottom explains restart markers instead of offering a focused live panel. This directly supports removing non-live options from the gear panel rather than redesigning their graphics behavior.

### Seen — G720 run

`proto-native-11.log` records source `b8e2e47…`, Mali-G720 MC7, direct system Vulkan 1.3, FIFO, successful device resets, Bink logo/menu/FMV opens with audio where present, and progress beyond frame 65,536. It records 1,024 and 2,048 cumulative texture-creation milestones. This is evidence for preserving the working Bink/FIFO/shadow defaults, not for a new rendering rewrite or universal device claim.

`app_process64_d3d9-8.log` corroborates successful Vulkan/D3D9 initialization, repeated FIFO swapchain resets, and no supplied fatal rendering error. The texture line is explicitly cumulative/non-resident; it is not proof of a leak. Large replacement packs nevertheless justify avoiding permanent whole-file caches by design.

### Seen — plugin ABI

`PdHostApi` already carries a per-plugin `context`. ABI v1 ends at `get_config_int`. This permits appended per-mod typed getters in ABI v2 without global key collisions. Existing example plugins compare the host version exactly, so merely changing the host to v2 would break them. A manifest-selected plugin ABI is therefore required.

### Seen — signing workflow and permissions

The protected-signing step only selects `workflow_dispatch`; `repository_dispatch` releases select the disposable branch. The release job also requires `allow_ephemeral_release` for repository dispatch. This explains why the dispatch-based 0.14 release could not use the protected path. The release certificate guard also lacks a separate, explicit persistent-certificate transition control.

`gh secret list` failed with HTTP 403 `Resource not accessible by integration`. **Unknown:** whether all four protected signing secrets exist and whether a maintainer has configured a new persistent keystore. No private key can be recovered from 0.14's APK, and no 0.14-compatible 0.15 signature is promised.

## External design research

- Cloth Config documentation, https://cloth-config.com/ and the Mod Menu API summary, https://github.com/mudkjp/mod-menu: established per-mod screens use toggles, bounded sliders, choices, validation, defaults, and short tooltips. **Inferred:** ProtoDroid should provide the same useful control vocabulary with its own schema and UI, not import Minecraft code.
- `SansNope/UnleashedRecomp-Android`, especially `ModSchemaJson.java` and `ModSettingsActivity.java` at repository `main` inspected 2026-11-01: per-mod `ConfigSchemaFile` values support typed options and atomic INI updates; release notes also describe avoiding recursive scan budgets for large mods. The repository is GPL-3.0, so this is behavioral/design research only; ProtoDroid's parser, schema, and UI will be independently implemented.
- PPSSPP texture replacement reference, https://github.com/hrydgard/ppsspp-site/blob/main/docs/reference/texture-replacement.md: manifest-keyed replacements and explicit mip/hash contracts are established. **Inferred:** retain ProtoDroid's SHA-256 keys and DDS validation; do not imply PPSSPP manifest, PNG, arbitrary-resolution, filtering, or rendering compatibility.
- Android immersive guidance, https://developer.android.com/develop/ui/views/layout/immersive: hide status/navigation bars with transient swipe reveal rather than attempting to disable system navigation permanently. **Inferred:** apply this to launcher activities while preserving cutout/inset handling.
- Earlier pinned references in `re/protodroid-014-followup-plan.md` remain applicable for import staging, path containment, native hooks, and independent Prototype Fix behavior.

## Design conclusions

### Live gear panel

**Inferred:** expose only settings that can take effect safely in the current process: FPS limit/effective battery saver, menu pacing, FPS overlay, and touch/gamepad controls. Keep graphics, drivers, engine fixes, texture dumping, and mod policy in the launcher. `unlock_menu_fps` already affects each later Bink wait decision; add an atomic live override initialized from the existing default-On setting. FPS/battery changes must compute one effective limiter target and update ADPF together. No restart button or restart asterisks remain in this panel.

### Mod configuration

**Inferred:** use independent author-declared options in `protodroid-mod.ini`, with toggle, bounded slider, choice, and single-line text controls; saved values go to the mod's own `protodroid-settings.ini`. Saves are atomic and preserve unknown safe values. The editor shows short help on focus, hover, touch/hold, and control changes instead of long page copy.

**Inferred:** package `mod.api=1` remains compatible. `[content] plugin_api=2` opts a native plugin into ABI v2. ABI-v1 packages receive the original version and original structure size; v2 receives appended typed per-mod getters using the existing context. This avoids the exact-version rejection in existing plugins.

### Large packs and imports

**Seen:** imports currently cap archives at 512 MiB, extracted data at 1 GiB, entries at 4,096, and mappings at 4,096 replacements. Library scans deeply open every referenced DDS. Native first use reads a whole DDS and duplicates its levels into retained vectors.

**Inferred:** increase bounded import/mapping budgets for full-map projects, retain streaming quota checks, and keep library listing shallow while requiring deep validation on import/enable and before native launch preparation. Replace retained whole-file DDS payloads with validated metadata/file offsets and lazy level reads. This is an I/O/memory-lifetime improvement, not a claim of faster rendering or support for different texture dimensions/formats.

### Launcher/updater/help

**Seen:** the boot curtain initially covers nearly all artwork with opaque black, explaining the dark-intro/bright-after transition. **Inferred:** keep artwork visible from frame one and use a narrow red transition accent instead. Add immersive mode and restrained existing-motion polish; do not add a heavy rendering pipeline.

The updater already downloads the exact release APK URL. **Inferred:** retain verified in-app download and make the recovery action open the fetched release's exact page (or `releases/latest` before metadata is available), with concise `OPEN RELEASE` labeling.

Engine explanations belong in Setup/Driver progressive help. Mods loses its engine-settings tab and engine setting persistence/UI code.

### Diagnostic tour

**Seen:** each phase repeats a durable `requested` record, writes/fsyncs a marker and directory, then writes another durable `running` record. Completion also syncs. This creates avoidable storage latency without adding coverage. Current phases retain separate shadow, action, gameplay-dispatch, and Present checks.

**Inferred:** remove redundant sync/logging, retain atomic durable current-phase/failure/result markers, and add per-phase elapsed time, exact action counts, dispatch/Present counts, and timing snapshots. Do not shorten action dwell, claim visual verification, or turn skipped physical-touch/Bink coverage into a pass.

## Remaining unknowns / non-goals

- Physical-device launcher visuals, mod loading, large-pack memory behavior, tour runtime, shadows, and update installation remain unverified until tested on hardware.
- No unchanged Windows x86 plugin compatibility, arbitrary-resolution texture replacement, new shader/render path, universal Vulkan support, stable 60 FPS, or exhaustive crash coverage is promised.
- Issue #21's required-UV failure and the supplied G720 logs do not justify a speculative rendering/threading change.
- Protected signing secrets cannot be inspected with the current token. Workflow corrections can be verified; actual persistent-key installation cannot be claimed from this environment.
