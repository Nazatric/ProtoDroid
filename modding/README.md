# ProtoDroid mod authoring

ProtoDroid supports read-only loose-file overlays, keyed DDS texture replacement, and independently built **Android ARM64** native plugins. Original game files and shared saves are not patch targets.

Windows x86 ASI/DLL binaries do not load. Port the source and build an AArch64 shared object; do not rename a Windows binary.

## Package layout

```text
my.mod/
  protodroid-mod.ini
  cover.png                 # optional PNG/JPEG/WebP
  files/                    # optional loose-file overlay
  textures.ini              # optional keyed DDS mapping
  lib/arm64-v8a/
    libmy_mod.so            # optional native plugin and dependencies
  protodroid-settings.ini   # created by ProtoDroid, not required in the ZIP
```

A minimal manifest:

```ini
[mod]
schema = 1
api = 1
id = my.mod
name = My mod
version = 1.0
author = Your name
description = A short player-facing description.
priority = 100

[content]
files = files
# textures = textures.ini
# plugin = lib/arm64-v8a/libmy_mod.so
# plugin_api = 2
```

At least one content type is required. Paths must stay inside the package. Higher priority wins conflicts. Imports start disabled; enable/disable and option changes apply on the next Play.

`mod.api` is the package schema and remains **1**. `content.plugin_api` selects the native plugin ABI: omit it for ABI v1, or set `2` for the appended configuration API. ABI-v1 packages keep the original host version and structure size.

## Per-mod options

Declare options in the same manifest. ProtoDroid creates a Configure button and saves values to that mod's own `protodroid-settings.ini`.

```ini
[setting.enabled]
type = toggle
label = Enable adjustment
description = Turns this effect on or off.
default = 1
group = Gameplay
order = 10

[setting.amount]
type = slider
label = Adjustment
description = Amount applied by the plugin.
default = 5
min = -20
max = 20
step = 0.5
group = Gameplay
order = 20

[setting.mode]
type = choice
label = Mode
options = Quiet|Detailed
default = Quiet
group = Diagnostics
order = 30

[setting.note]
type = text
label = Log note
description = A short note written to the native log.
default = Hello
max_length = 80
group = Diagnostics
order = 40
```

Rules:

- Up to 64 options per mod. IDs use lowercase letters, digits, `.`, `_`, or `-`.
- `order` is optional; lower values appear first. `group` and `description` are optional.
- Sliders require finite `min`, `max`, `default`, and a positive `step`; the range must contain at most 10,000 whole steps.
- Choices are separated by `|`, and the saved value is the choice text rather than its position.
- Text is trimmed, single-line, and limited by `max_length` (1–1024).
- Saved values are validated and written atomically. Unknown safe values are preserved for forward compatibility.

The editor is a ProtoDroid UI, not a Fabric, Mod Menu, Cloth Config, HMM, or UMM compatibility layer.

## Native configuration API

Include `modding/include/protodroid_plugin.h` and opt into `plugin_api = 2`. In addition to the v1 guest/detour/file/event functions, ABI v2 provides:

```c
bool enabled = host->get_mod_boolean(host->context, "enabled", true);
double amount = host->get_mod_number(host->context, "amount", 5.0);

char mode[32];
if (!host->get_mod_string(host->context, "mode", "Quiet", mode, sizeof(mode))) {
    /* Missing buffer space or another output error; choose a safe fallback. */
}
```

These getters read only the current plugin's settings. Missing or invalid values use the supplied defaults. String results are copied into caller-owned storage. The old `get_config_int` remains for v1 compatibility but reads the process-wide engine INI, so new plugins should use the per-mod getters.

The complete example in `modding/example/` uses all four option types and demonstrates an ABI-v2 plugin. Build its importable ZIP with:

```sh
ANDROID_NDK_HOME=/path/to/ndk modding/example/build-package.sh
```

Import `build/protodroid-example-arm64.zip`, review it, then enable it only when you want its FOV adjustment. Later detours wrap earlier ones; a detour's original pointer calls the previous detour or real function.

Bundle dependent ARM64 libraries under `lib/arm64-v8a` and use an `$ORIGIN`-relative runpath when needed. Plugins are trusted native code and can crash the game.

## Texture packs and large projects

`textures.ini` maps ProtoDroid's versioned SHA-256 keys to same-format, same-dimension DDS files:

```ini
[hashes]
0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef = characters/alex.dds
```

Use **Dump original textures** in Setup to produce original-format DDS files and starter keys. These are not PPSSPP hashes.

- Supported formats: A8R8G8B8, X8R8G8B8, DXT1, DXT3, and DXT5, with bounded 2D mip chains.
- A replacement must match the original base dimensions and format. ProtoDroid does not resize textures or add rendering features.
- Dynamic textures, render targets, cube maps, partial locks, and unsupported layouts are outside this contract.
- Missing replacement mips retain the original mip.
- Large packs can contain up to 65,536 ZIP entries/replacements within the importer's 4 GiB archive and 8 GiB extracted-byte quotas. Storage space still needs to cover the temporary archive and extracted copy.
- Native replacement reads are lazy: headers/offsets stay indexed, while only the requested mip is read temporarily. This avoids retaining every pack payload in memory; it is not a rendering-speed guarantee.

See `re/native-mod-platform.md` for the exact lock, key, path, and compatibility boundaries. Host/CI tests do not prove a mod's appearance or behavior on every device.
