# ProtoDroid

ProtoDroid is a native Android port of **Prototype (2009)**. It runs the original game engine as ARM64 code rather than using Wine or a full x86 emulator. The APK contains the port and its open-source dependencies; you must provide your own legally obtained game files.

The Android application ID is **`com.protodroid`**.

## Architecture

- **Static ARM64 recompilation:** `prototypeenginef.dll` is translated from 32-bit x86 into C, generated for the engine's rebased address, and compiled for Android. The runtime provides guest memory, x86-like memory ordering, threads, fibers, and the required Microsoft CRT behavior.
- **Custom Win32 and input bridge:** native implementations cover the Win32, DirectInput 8, XAudio2 2.3, Bink 1.9, and launcher interfaces used by the game.
- **Direct3D 9 on Vulkan:** a generated D3D9 bridge forwards drawing directly to DXVK. Android patches address Mali and Adreno driver behavior, descriptor compatibility, pipeline compilation, presentation, and persistent shader caching.
- **Bink through FFmpeg:** loose files and RCF-resident movies are decoded by a minimal FFmpeg build. Registered YUV planes are copied directly into the textures supplied by the game.
- **XAudio2 through FAudio:** FAudio provides the audio engine and voice processing, with SDL audio output and an Android-friendly pacing/mixing configuration.
- **SDL2 and Android UI:** SDL owns the window and platform events. A native Android touch layer provides menu controls, gameplay controls, gamepad support, a layout editor, and performance settings.

## Current Features and Optimization Work

### Graphics and performance

- Direct DXVK draw dispatch without minimap relocation, draw inspection, or manual per-draw wrappers.
- Read-only locks never copy back or dirty DXVK resources. Ranged vertex/index buffer locks upload only the requested range.
- Optimized DXT1/DXT3/DXT5 decoding, including ARM64 NEON paths and randomized differential tests against the scalar decoder.
- Texture mip reduction, optional 16-bit DXT1 output, effective 1x sampler handling, and configurable anisotropy.
- Stable FIFO presentation on Android. `CreateDevice` and every `Reset` use interval `1`, preventing post-cutscene presentation-mode changes.
- Persistent DXVK state caching with a stable filename, migration from the former `app_process64` filename, flushed headers/entries, and writer-queue draining during shutdown.
- Mali-specific descriptor and pipeline workarounds, including support for GPUs without BC texture compression or robust null descriptors.
- Turnip support for Adreno through libadrenotools, with system-driver fallback and an optional user-supplied driver.
- ADPF performance hints, unrestricted CPU scheduling, hot-function ARM64 replacements, I/O timing, and optional frame/audio diagnostics.

### Movies and audio

- Full-screen Bink playback, including NIS cutscenes and ambient menu movies.
- Neutral black initialization for unused movie planes without clearing valid decoded frames during close or re-registration.
- Direct host `pread64` streaming for loose files and RCF slices, without holding a global mutex during disk reads.
- Bounded decode work, retained presentation frames, contiguous plane copies, negative-pitch support, and correct Bink ABI behavior.
- **Unlock menu FPS**, enabled by default: silent/ambient movies keep their normal video timing while `BinkWait` uses the guest's non-blocking poll branch instead of sleeping the menu render thread.
- Retained-master audio output to avoid unnecessary output-thread recreation, plus source-voice and engine-shutdown cleanup.

### Controls

- MENU-mode absolute mouse clicks, plus separate GAME-mode camera look, virtual joystick, action buttons, and power wheels.
- Configurable button opacity, size, positions, vibration, camera speed, invert Y, edge sprint, and optional extra buttons for disguise powers, Web of Intrigue, disguise, and Alex form.
- Gamepad detection and an option to hide touch controls while a gamepad is connected.

## Requirements

- An ARM64 Android device with working Vulkan support. Android 10 or newer is recommended.
- A PC copy of **Prototype (2009)**, Steam app 10150, build 19788432, for the game data and rebuild inputs.
- Enough free storage for the game files, shader cache, and logs.

## Installation

1. Install the signed `prototype.apk` and launch **Prototype**.
2. Copy the game files to one of these locations:
   - **Recommended shared-storage location:** `/storage/emulated/0/Prototype/`
   - A supported subdirectory: `/storage/emulated/0/Prototype/game/`
   - App-specific storage: `/storage/emulated/0/Android/data/com.protodroid/files/game/`
3. The game directory must include `prototypeenginef.dll` and the original archives, folders, and other data from your copy of the game. The APK does not supply those files.
4. When using shared storage, grant file access on the setup screen. For the app-specific `Android/data` location, copy from a PC over USB on Android 11 or above; on Android 10 or below, a file manager with storage access can be used.
5. Open the app after copying the files. Once file access is granted, the setup actions become **Settings** and **Play**. The launcher detects game data automatically; press **Play** when you are ready to start it.

The application ID and app-specific directory are both `com.protodroid`. The shared game directory remains `/storage/emulated/0/Prototype`, so game files do not need to be duplicated.

## Saves and Profiles

With shared file access granted, profiles and saves such as `profile.bin` and `slot-*.bin` are read and written in `/storage/emulated/0/Prototype/Saves/`. The folder is created automatically. Files added, replaced, or deleted there by a file manager are used by the game.

The first run with this folder migrates existing saves from the previous shared `/storage/emulated/0/Prototype/` location and from `/storage/emulated/0/Android/data/com.protodroid/files/native_c/Users/Player/Documents/Prototype/`, without overwriting files already in `Saves`. Current shared saves take priority over retained app-specific backups. Migration runs only once, so later deletion of a save does not restore an old copy. Previous files remain as backups. Without shared access, the legacy app-specific save directory continues to be used.

## Configuration

The optional `protodroid.ini` file can be placed at:

- `/storage/emulated/0/Prototype/protodroid.ini`
- `/storage/emulated/0/Prototype/game/protodroid.ini`
- `/storage/emulated/0/Android/data/com.protodroid/files/protodroid.ini`
- The detected game directory or its parent directory

Start with [`android/protodroid.ini.example`](android/protodroid.ini.example). Keys use `key = value` syntax. Every key can also be supplied as `PROTODROID_<KEY>` in uppercase; an environment value overrides the file.

Frequently used keys include:

| Key | Purpose |
| --- | --- |
| `render_height` | Internal render height; Android defaults to 720. Width follows the display aspect ratio. |
| `texture_skip_mips` | `0` full textures, `1` half resolution, `2` quarter resolution. |
| `texture_skip_min_size` | Minimum texture size eligible for mip reduction. |
| `max_anisotropy` | Texture anisotropy limit: 1, 2, 4, 8, or another supported value. |
| `fps_limit` | Host frame limit: 0/off, 30, 45, or 60. |
| `battery_saver` | Enables the battery-saving frame policy. |
| `unlock_menu_fps` | `1` enables non-blocking silent/ambient movie polls; `0` restores blocking waits. Default: `1`. |
| `audio_driver` | `aaudio` or `openslES`. |
| `audio_master_reuse` | Reuses the retained mastering output when the game recreates voices. |
| `vulkan_driver` | `auto`, `system`, or `turnip`. |
| `present_stats` | Enables DXVK presentation diagnostics. |
| `audio_stats` | `1` logs real output underruns (default), `2` logs every timing window, `0` disables audio diagnostics. |
| `detailed_diagnostics` | `1` enables timestamped native/device diagnostics; default `0` avoids their timing/sample overhead. Shared log mirroring continues either way. |
| `detailed_diagnostics_interval` | Diagnostic window in seconds, clamped to 1–60. Default: `5`. |
| `pipeline_timing` | Lightweight DXVK pipeline-call timing; defaults to the detailed-diagnostics setting. |
| `pipeline_trace` | Enables expensive Mali pipeline tracing and shader dumps for debugging only. |
| `dxvk_config` | Additional DXVK options; separate entries with commas in this file. |
| `import_profile` | `1` profiles per-thread guest and Win32/D3D9 wall time every ten seconds for stall investigation. |
| `watchdog` | Hang diagnostics. |

Android presentation is intentionally fixed to FIFO/interval `1`; desktop builds retain the optional presentation-mode settings. An explicitly selected FPS cap continues to use the host limiter, while unlimited mode leaves presentation pacing to FIFO. With detailed diagnostics enabled, periodic reports separate Present-to-Present timing from Present() wall time so vsync waits are not mistaken for measured CPU work. Each native startup log also records the exact source revision supplied by CI.

## Launcher and In-Game Settings

The launcher's **Settings** page and the in-game gear overlay share the same saved options. Launcher changes are saved automatically and engine options apply when **Play** starts the game. In the in-game overlay, settings marked with `*` apply after restart; the FPS limit applies immediately.

The launcher groups options into **Graphics**, **Game Video**, **Controls**, and **Interface**. It also provides **Low**, **Medium**, and **High** graphics presets. They change Resolution, Textures, Anisotropy, Anti-Alias Level, Shadow Detail, and Graphic Quality together, leaving FPS, battery, menu-FPS, and control preferences untouched. Choose **Custom** or adjust any preset-covered row to keep an individual combination.

- **Graphics:** Resolution, Textures, Anisotropy, FPS limit, Battery saver, and Unlock menu FPS (default **On**).
- **Game Video:** Anti-Alias Level, Shadow Detail, and Graphic Quality. These use the game's own GameDB settings and render paths. **Game default** preserves the engine's device-dependent choice; explicit levels apply on the next Play or restart.
- **Controls:** Button size, Vibration, Hide with gamepad, Stick edge sprint, Camera speed, Invert Y, and Extra buttons (default **Off**).
- **Interface:** Opacity and Show FPS.

The **LAYOUT** button opens the control-layout editor. Drag controls to reposition them, select a control to resize it, or reset the layout.

## Logs and Shader Cache

Depending on storage access, logs are available in the app-specific directory and mirrored to shared storage:

- `/storage/emulated/0/Android/data/com.protodroid/files/`
- `/storage/emulated/0/Prototype/logs/`

Important files:

- `proto-native.log`: current native/engine run
- `proto-native.prev.log`: previous run
- `app_process64_d3d9.log`: DXVK and Vulkan diagnostics
- `prototypeenginef.dll.dxvk-cache`: persistent pipeline state cache
- `turnip_failed`: present only after a Turnip initialization failure

Detailed diagnostics timestamp native and DXVK events with monotonic seconds and thread IDs. Five-second windows include Present and engine timing percentiles, fixed long-frame thresholds and worst events, texture and file I/O, process/per-thread CPU, memory, ADPF, and Android thermal/headroom data where supported. Slow file events above 50 ms retain their path and thread. Present() wall time is not labeled as GPU time, and threshold counts are not claimed to be physical display-drop counts. The lightweight pipeline log does not dump shaders or enable the expensive `pipeline_trace` path.

No state-cache seed is bundled. If the app-private cache is missing or header-only, startup can restore a populated `prototypeenginef.dll.dxvk-cache` (or legacy `app_process64.dxvk-cache`) from the app-specific directory or the shared locations above. Existing populated or invalid private caches are not overwritten by recovery; DXVK performs the final entry validation and reports the actual count read. DXVK queues cached pipelines for early compilation as their game shaders become available; variants that have never been encountered cannot be known or preloaded in advance.

Populated private caches are mirrored periodically to shared storage with atomic replacement, using a single-flight helper thread rather than blocking the engine thread. Shutdown retains a synchronous final copy. An interrupted backup does not truncate the previous copy. The live DXVK writer stays in app-private storage; the mirror is a recovery backup, not a power-loss-durability guarantee.

The 30-second native performance report includes presented-frame statistics plus interval texture timing: DXT decode, host texture creation, and host upload calls with call count, total milliseconds, and worst-call milliseconds. These measurements help distinguish CPU conversion, allocation, and upload/sync stalls; they do not claim that any one counter is the sole cause of a frame spike.

For bug reports, include the complete logs, phone model, SoC/GPU, Android version, APK commit, and exact steps that reproduce the issue. Also use `adb logcat -s protodroid` when logcat access is available.

## Building

A Linux build environment needs the Android SDK/NDK, JDK, SDK build-tools, CMake, Ninja, Meson, Python 3, `capstone`, and `pefile`. Turnip additionally requires the tools listed in `android/build-turnip.sh`.

Set `PROTODROID_ENGINE` to the original `prototypeenginef.dll` when it is not in the default location. The engine must be generated for base `0x7c000000`, matching `src/native/guest.h`.

```sh
# Build SDL2, FAudio, minimal FFmpeg, and patched DXVK.
android/build-deps.sh arm64-v8a \
  "$HOME/protodroid-data/android-deps-src" \
  "$HOME/protodroid-data/android-deps/arm64-v8a"

# Optional Adreno Turnip build.
android/build-turnip.sh \
  "$HOME/protodroid-data/android-deps-src" \
  "$HOME/protodroid-data/android-turnip"

# Generate ARM64-bound recompiled engine sources.
python3 -I tools/recomp/data_entries.py
python3 -I tools/recomp/recomp.py \
  --functions all --base 0x7c000000 --split 400 \
  --out build/recomp/native

# Build the primary ARMv8.2 native library.
tools/cool_make.sh -C src/native PLATFORM=android ABI=arm64-v8a

# Build the ARMv8.0 compatibility library used on older cores.
tools/cool_make.sh -C src/native PLATFORM=android ABI=arm64-v8a \
  ARM_ARCH=armv8-a OUT=../../build/android/arm64-v8a-v80

# Package, zipalign, sign, and verify build/apk/prototype.apk.
android/build-apk.sh arm64-v8a
```

`tools/cool_make.sh` runs a parallel build and pauses when the host temperature reaches its configured limit. Ordinary `make` with the same variables also works.

## Tests

Host tests use strict warnings and UndefinedBehaviorSanitizer. They cover DXT decoding, sampler/lock rules, presentation normalization, state-cache resolution/migration, Android version increments, and Bink input/ABI/planes/audio/pacing.

```sh
# Graphics helpers and DXT differential tests.
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror \
  -fsanitize=undefined -fno-sanitize-recover=all -Isrc/native \
  src/native/d3d9_dxt.cpp src/native/tests/graphics_test.cpp \
  -o /tmp/graphics_test
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 /tmp/graphics_test

```

The production Bink harness and its minimal FFmpeg setup are automated in `.github/workflows/build-apk.yml`. The additional hot audio/skinning differential tests use `src/native/tests/run_hot_test.sh` and require the original engine binary. CI runs them on native ARM64 with the original DLL fetched read-only from `TestingFiles`; the skinning suite compares 2,000 randomized batches, including larger record counts and both output alignments.

## CI/CD

- **`build-apk`**: runs host UBSAN tests, builds ARM64 dependencies, generates the real recompiled engine, compiles native libraries, packages and signs the APK, verifies embedded libraries/DXVK and the signed package version, and uploads `prototype-apk` plus diagnostic artifacts. Ordinary pushes, including log-only uploads to `main`, do not publish releases.
- **`android-deps`**: independently builds the ARM64 and x86_64 dependency sets and the Adreno Turnip package.

`android/AndroidManifest.xml` is the version source of truth. Before each new approved release cycle, run `python3 tools/bump_version.py` once, add the version's notes to `CHANGELOG.md`, and commit both changes. The version tool advances both values together (`0.1`/code `1` → `0.2`/code `2` → `0.3`/code `3`). Packaging verifies those exact values in the signed APK. After an exact-commit green run on `main`, an explicitly approved manual workflow dispatch with **Publish a new versioned GitHub release** enabled creates `protodroid-v<version>` using the matching changelog section. The release job refuses reused or out-of-order visible versions and `versionCode` values instead of replacing an existing release.

A release candidate should have an exact-commit green workflow, real native compilation rather than a dummy/prebuilt fallback, successful `apksigner verify`, and a verified `prototype.apk` artifact. Compilation and host tests do not replace on-device gameplay and visual testing. A version bump does not by itself resolve signature conflicts or every Android installation error.

### Signing and in-place updates

Local packaging uses the Android debug keystore by default. Set `KEYSTORE`, `KEYSTORE_PASS`, `KEY_ALIAS`, and optionally `KEY_PASS` to use another key. `KEY_PASS` defaults to the store password (normally required for PKCS#12); JKS also supports a distinct key password. Ordinary branch/PR CI artifacts use a disposable identity and are not release-eligible; protected release secrets are read only for an explicitly approved release dispatch on `main`.

Protected releases require these repository Actions secrets:

- `PROTODROID_ANDROID_KEYSTORE_BASE64`: the keystore encoded with `base64 -w 0` (Linux) or `base64` without line wrapping (macOS).
- `PROTODROID_ANDROID_KEYSTORE_PASSWORD`
- `PROTODROID_ANDROID_KEY_ALIAS`
- `PROTODROID_ANDROID_KEY_PASSWORD`

Keep an offline backup of the original keystore and passwords; never commit them. The release workflow refuses a missing/incomplete protected signing configuration, records the certificate SHA-256 digest, and rejects dummy or prebuilt native inputs. Ephemeral signing is rejected by default and requires a separate explicit release-dispatch override. Once protected signing is configured, subsequent releases keep the same identity and can update each other in place.

Version 0.7 and earlier CI artifacts were signed with per-run keys whose private keys are not available. A newly signed APK cannot directly update a differently signed installation: Android requires a one-time uninstall first. Shared saves under `/storage/emulated/0/Prototype` are separate from the app and remain there; back up any important app-specific data before uninstalling.

## Credits and Legal

Original Android port and recompilation research by **[VinTeRuS](https://github.com/VinTeRuS)**. ProtoDroid continues with contributions from the community.

Prototype is a trademark of its respective owners and was developed by Radical Entertainment. This project is not affiliated with or endorsed by Activision or Radical Entertainment. Provide your own game files; no DRM bypass is included.
