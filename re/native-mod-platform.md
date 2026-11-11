# Native mod platform — evidence and implementation contract

Status: 0.14's base contract plus 0.15 Lua/dependency/ABI-v3 extensions. Host tests and exact-commit ARM64 CI cover the new implementation; this document does not claim physical-device validation. Code commit `ea96126ed03c52a5f5c042a747b68e9022494896` passed CI run [41338443987](https://github.com/Nazatric/ProtoDroid/actions/runs/41338443987), including Android Java tests, native ARM64 tests, source-built examples, and signed APK verification.

## Research method and provenance

The supplied Prototype Fix ASI, Ultimate ASI Loader, engine DLL, ProtoDroid source, and the following independent projects were inspected as read-only research inputs. Work and disassembly were done on copies under `/tmp`; the game files were not edited.

- **Seen:** `aicodeking/leanrea/leanrea/SKILL.md` requires inventory before decompilation, batched probes, and seen/inferred/unknown labels. The probes and disassembly are under `/tmp/protodroid-mod-platform-research/`.
- **Seen:** `hedge-dev/UnleashedRecomp` resolves prioritized include directories without changing save paths and caches path lookups. `SansNope/UnleashedRecomp-Android` keeps enabled order and visibility in its Android manager.
- **Seen:** `utkarshdalal/GameNative` uses staged extraction, validated identifiers, rollback-promotion, case-insensitive collision checks, and interruption tests.
- **Seen:** `hrydgard/ppsspp` uses a `textures.ini` hash-to-file mapping, validates pack paths, and tests missing/unknown replacements. Its implementation is GPL; no PPSSPP implementation is copied into ProtoDroid.
- **Seen:** `VinTeRuS/proto-re` reconstructs `FileOpenRequest::DoRequest`, including the drive vtable open slot and loose-file result handling.
- **Seen:** Dobby exposes `DobbyHook(address, replacement, &original)` and builds a static AArch64 Android library under Apache-2.0. ShadowHook is a credible MIT alternative, but Dobby is selected for the first implementation because a static dependency fits the existing non-Gradle Makefile and needs no extra shared-library loader policy.
- **Seen:** DXVK 2.5.3 has `d3d9.maxAvailableMemory`; it is a configurable memory report cap, not proof of physical VRAM.

## Engine facts established by probes

All addresses below are for the supplied Steam engine. Native recompiled addresses are `original VA + 0x6c000000`.

| Feature/path | Seen evidence | Native implementation decision |
|---|---|---|
| x86 ASI loading | PE32/x86 code, Windows DLL proxy and `LoadLibrary` initialization | No unchanged ASI/DLL compatibility claim. Only native ARM64 plugins are loaded. |
| CPU thread count | Engine CPUID path; native `RecompCpuid` already reports a controlled SSE2-class CPU/core count | Keep the native implementation; do not duplicate the Windows byte patch. |
| Billboard advertisements | Engine network path and existing native networking stubs | Keep native network behavior; no speculative billboard rewrite. |
| 15 ms simulation gate | `0x10897bf3` compares elapsed time with `0x3a98`; `0x10897bf8` skips the tick | Add an exact generated-source policy patch at recompiled `0x7c897bf8`, gated at runtime and off by default. |
| Button struggle timing | `0x100cc6c0`, `ret 8`; its second argument at entry `esp+8` feeds accumulation and decay | When and only when tick-limit removal is active, use monotonic elapsed time for that argument. Reset timing on a new action object. |
| FPS limit | Existing native frame limiter and `fps_limit` setting | Use the native limiter; preserve default-On `Unlock menu FPS`. |
| Custom/window resolution and borderless mode | Windows EXE/DLL display overrides; Android already owns the fullscreen SDL window and render sizing | Keep native fullscreen/render-height handling. Do not port EXE patches or add a second window mode. |
| DInput HID filtering | The Windows fix avoids slow enumeration of arbitrary HID devices; the native DInput bridge already exposes only keyboard/mouse, with gamepads handled separately | Keep the existing controlled input path; do not add native HID enumeration merely to filter it again. |
| FOV offset | `pure3d::Camera::AdjustFOV` at original `0x106dcfe0`, first float argument at `esp+4` | Optional detour adds a finite fractional degree offset before calling the original and clamps the resulting FOV. |
| Shadow map size | Original `0x10715980` writes output dimensions through arguments at entry `esp+8` and `esp+12`; the supplied fix documents 512–8192 | Call original, then scale its width/height ratio to the optional selected maximum; never force square maps. Unsupported values leave the original unchanged. |
| VRAM report | Windows patch and DXVK's supported memory option | Optional `d3d9.maxAvailableMemory`; default leaves DXVK unchanged. |
| Sample rate | Windows engine patch; Android uses native FAudio | Keep FAudio; no duplicate generated-audio rewrite. |
| Developer menu | Prototype Fix writes `RAD_RELEASE` over the engine build-type string | Optionally replace a uniquely found `RAD_FINAL` guest-data string with `RAD_RELEASE`, with an exact-byte/count guard. |
| Loose files | `FileOpenRequest::DoRequest` original `0x1064c820`; drive slot 10 opens by name | Detour only when an overlay/loose candidate exists. Try the existing guest drive open, adopt its result on success, otherwise call the untouched original. |
| Direct calls | Generated C calls other generated functions directly | `RecompDispatch` alone is not a hook. Use function-level ARM64 detours for plugins and the selected native features. |
| Verbose logging | Prototype Fix exposes optional diagnostic logging | Keep ProtoDroid's persistent native/mod logs; no separate Windows log-file path is needed. |

**Inferred:** monotonic QTE timing should preserve behavior better than frame-count timing when the 15 ms gate is removed. This needs on-device QTE validation; it is not presented as observed gameplay.

**Unknown:** actual menu appearance after the build-type change, high-resolution shadow performance on each GPU, and every third-party plugin's behavior. These remain experimental optional features with persistent logs.

## Mod package contract

A mod is a directory below the launcher-selected mod root:

- Shared storage with all-files access: `/storage/emulated/0/Prototype/mods/<id>/`.
- Otherwise: the existing app-specific external files directory `mods/<id>/`.

`protodroid-mod.ini` uses schema 1:

```ini
[mod]
schema = 1
api = 2
id = example.textures
depends = example.library
name = Example textures
version = 1.0
description = Replaces selected original-format textures.
author = Example author
cover = cover.png
priority = 100

[content]
files = files
textures = textures.ini
script = scripts/main.lua
script_api = 1
plugin = lib/arm64-v8a/libexample.so
plugin_api = 3
```

Every content key is optional, but at least one is required. Paths are relative, canonical, and cannot escape the mod directory. Symlinked package roots are rejected for launch/enable, and marker/diagnostic writes do not follow pre-existing links. Higher priority wins file/texture conflicts. Ordinary packages may retain `mod.api = 1`; scripts, dependencies, and native ABI 3 require `mod.api = 2`. `content.plugin_api` selects native ABI 1, 2, or 3, and `content.script_api` is currently 1. Native plugins and scripts also support an optional exact `engine_sha256` in `[mod]`. Dependencies initialize before dependants; missing, disabled, incompatible, and cyclic graphs are rejected independently by Java and native launch validation.

- `files`: loose-file overlay root. Original game files are never modified. Save paths and write/create/truncate modes are excluded. Read-only Win32/CRT opens and existence/metadata probes resolve replacements; wildcard directory enumeration is not merged.
- `textures`: line-oriented `hash = relative/file.dds` mapping, optionally in `[hashes]`. The hash is ProtoDroid's versioned SHA-256 texture key, not a PPSSPP hash.
- `plugin`: ELF64 little-endian AArch64 shared object. Windows ASI/DLL files are rejected with a translation-required error.
- `script`: bounded text-only Lua 5.4.8 entrypoint with per-state allocator/instruction limits, restricted libraries, guarded engine memory/calls/hooks, named symbols, settings, and lifecycle/custom events. See `modding/README.md` for the complete API and trust boundary.
- `cover`: PNG/JPEG/WebP decoded with bounded sampling by the launcher.
- `enabled`: launcher-created marker. Imported mods start disabled. Changes apply on the next Play.
- `setting.<id>.*`: optional author-defined toggle, bounded/stepped slider, named choice, or bounded text controls. See `modding/README.md` for the schema and limits.
- `protodroid-settings.ini`: generated per-mod values; safe unknown keys survive edits, and updates are atomic.

ZIP import uses a local staged archive and staging directory, canonical-path checks, case-insensitive duplicate rejection, entry/count/byte limits, and atomic same-filesystem promotion. An existing ID is not silently overwritten. Removal is confirmed and restricted to a direct child of the mod root. Legacy ZIPs without metadata receive a generated manifest: `textures.ini` selects a texture pack, an ARM64 library selects a native plugin, and other files select a loose overlay. One safe top-level wrapper is flattened only inside private staging before canonical-path/content validation, so wrapped texture, plugin, and loose-file layouts keep their intended type. Nested or ambiguous wrappers are not recursively unpacked. Unsupported Windows native binaries fail explicitly.

Native plugin files are validated and copied to a fresh app-private `noBackupFilesDir/mod-native-<launch>` before launch. Native code never `dlopen`s a shared-storage path. Older private launch directories are cleaned only after the environment points at the fresh path, so a cleanup failure cannot resurrect removed/disabled plugins. Dependent ARM64 libraries must be bundled under the mod's `lib/arm64-v8a` tree and linked with an `$ORIGIN`-relative runpath when required.

## Texture replacement contract

ProtoDroid already maintains guest shadow buffers for ordinary locks and adapted DXT locks. The replacement point is before `ShadowUnlock` write-back and before `AdaptedUnlock` decoding. Disabled mods add no hash, file I/O, format conversion, or texture-allocation changes.

For a full 2D level lock, the key is:

```
SHA256("ProtoDroidTextureV1\0" || little-endian format,width,height,rowBytes,rows || tightly packed rows)
```

Pitch padding is excluded. DDS replacements must match the original dimensions and guest format. This first contract intentionally supports same-size replacement, not high-resolution texture resizing. A8R8G8B8/X8R8G8B8 and DXT1/3/5 are supported; unsupported layouts are skipped and logged. Replacement rows are expanded back to the original guest pitch before the existing upload/decode path. In 0.15, validated DDS metadata retains file offsets instead of every payload; only the requested mip is read temporarily. Import/listing validation is separated so large libraries do not open every DDS during a shallow scan.

Only full locks of static (`usage == 0`) 2D textures participate. Dynamic textures, render targets, cube maps, and partial rectangles are excluded. Identity is established at the full top-level lock; a mip locked before that first top-level identification is not retroactively replaced. Decoded/dropped read-only adapted locks are not hashed because they have no authoritative guest-input copy. Missing replacement mips retain the original mip.

Optional dumping writes original-format DDS files and a ready-to-pack `textures.ini` under `Prototype/texture-dump` (or the app-specific equivalent). Each hash is written once. No dump occurs in ordinary Play unless explicitly enabled.

## Built-in optional settings

These `protodroid.ini` keys are independent of imported packages and are off/preserved by default:

- `fov_offset = 0`: finite signed fractional degree offset, with the resulting FOV clamped to 30–150. The launcher offers common integer offsets; INI retains fractional values.
- `shadow_map_resolution = 0`: `512`, `1024`, `2048`, `4096`, or `8192`; scales the original map ratio rather than forcing square maps. Higher limits are device-dependent and default off.
- `remove_tick_rate_limit = 0`: removes only the evidenced 15 ms simulation skip. `fix_button_tap_tick_rate = 1` enables the inferred monotonic struggle-timing adjustment only when that gate is removed.
- `prefer_loose_files = 0`: enables the original loose-file open policy even without an imported overlay.
- `unlock_dev_menu = 0`: attempts the guarded unique build-type patch; ambiguity/refusal is logged and leaves data unchanged.
- `vram_report_mb = 0`: appends DXVK's `d3d9.maxAvailableMemory` report cap (clamped to 128–16384 MiB), not a physical-memory measurement.
- `dump_textures = 0`: enables original-format top-level DDS dumping and a ready-to-pack mapping.

Existing `fps_limit`, `render_height`, display, driver, audio, save, and default-On `unlock_menu_fps` behavior remains the implementation for those equivalents; no parallel Windows patch path is introduced.

## Sandboxed Lua and ARM64 plugin ABI versions 1–3

Lua packages use independent states, text-only chunks, per-mod settings, restricted libraries, bounded module loading, allocator/instruction budgets, and protected callbacks. The generated `mod_symbols.inc` index is reproducible from `re/functions.csv`; exact duplicate names use ascending-RVA ordinals. Scripts and native plugins share the dependency-ordered package graph. Uncaught script failures disable only that state, preserve the original detour path when possible, and write a persistent launch error. States and detour storage remain alive through teardown.

Public native source lives in `modding/include/protodroid_plugin.h`; complete native and Lua examples live in `modding/example/` and `modding/script-example/`.

The versioned C ABI provides only implemented capabilities:

- guest memory read/write;
- opaque guest-context register/stack access;
- guest function calls with stack arguments and optional `ecx`;
- exact engine-RVA function detours with per-target chaining: later registrations wrap earlier ones, and each original pointer reaches the previous detour or real function;
- mod-relative file overlay registration;
- engine-frame and shutdown events;
- mod/game paths, engine identity, legacy global integer configuration, and logging;
- ABI-v2-only per-mod boolean, number, and string getters, appended after the exact v1 host structure;
- ABI-v3-only exact named-symbol lookup and native-to-script event emission, appended after the exact v2 host structure.

Registrations occur during `ProtoDroidPluginInit`, before engine startup. Native plugins are process-lifetime optional code; disabling/removing takes effect on the next process. Shutdown callbacks run at the ordinary safe shutdown boundary; libraries are not unloaded while detours could still execute. Hook/registration failures are returned to the plugin and logged, not hidden.

The exact v1/v2 host sizes are exported as `PD_HOST_API_V1_SIZE` and `PD_HOST_API_V2_SIZE`; old plugins keep their original ABI and structure size. ABI 3 adds only the implemented symbol/event functions. No ABI claims source or binary compatibility with Windows ASI plugins. Existing plugins must be translated and rebuilt for AArch64.

## Launcher contract

A native Android Mods page is reachable beside Drivers. It provides:

- installed and active counts;
- ZIP/single-file import with clear supported-format guidance;
- cover, name, version, author, description, content type, and compatibility/load errors;
- background import, enable/disable, and removal operations with visible progress/error status;
- default-disabled imports, next-Play application, two-step removal, and separate executable-content trust confirmation;
- explicit warnings for native ARM64 plugins and engine-access Lua scripts, plus the fact that x86 ASI/DLL files are not loaded;
- a Configure action for packages with author-defined options, with reset/discard handling and atomic values;
- built-in engine policies remain in Setup rather than the mod library.

The page shares Setup's artwork, Barlow Condensed fonts, red/bone palette, and cut panels through a custom Android Canvas view. It retains 48 dp touch targets, bounded cover decoding, status announcements, keyboard/D-pad focus, scrolling/fling, and Android reduced-motion handling without adding a UI runtime. Engine INI writes are serialized, synced to a same-directory temporary file, and atomically replaced where supported; failures preserve the previous file and produce a visible main-thread warning.

## Validation and release boundaries

1. Host Java tests cover metadata, script source/traversal, dependencies/cycles, collisions, ELF architecture, enable/remove policy, and manifest errors.
2. Host C++ tests cover texture keys/DDS validation, path policy, native dependency ordering, Lua sandbox/memory/events/hooks, the shipped Lua example, and plugin ABI v1/v2/v3 layout.
3. Python source/signature tests verify the exact generated tick patch, selected engine targets, and no accidental default changes.
4. Real ARM64 CI builds Dobby, `libmain.so`, and the example plugin at the exact pushed commit; artifact metadata must match that commit.
5. Existing graphics, Bink, save, driver, tour, UI, and package tests must remain green.
6. No APK is produced for each small edit. Batch changes into one exact-commit ARM64 candidate. Keep 0.15 unmerged and unreleased; do not present that candidate as a published update.
7. CI is not physical-device confirmation. Mod loading, overlays, texture appearance, QTE timing, developer menu behavior, high-resolution shadows, and final on-device APK behavior remain device-dependent; release notes must not present host/CI evidence as physical testing.
