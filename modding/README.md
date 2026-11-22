# ProtoDroid mod authoring

ProtoDroid supports read-only loose-file overlays, keyed DDS texture replacement, sandboxed Lua gameplay/engine scripts, and independently built **Android ARM64** native plugins. Original game files and shared saves are not patch targets.

Windows x86 ASI/DLL binaries do not load. Port the source and build an AArch64 shared object; do not rename a Windows binary.

## Packages and dependencies

```text
my.mod/
  protodroid-mod.ini
  cover.png                 # optional PNG/JPEG/WebP
  files/                    # optional loose-file overlay
  textures.ini              # optional keyed DDS mapping
  scripts/
    main.lua                # optional API-2 Lua entrypoint
    lib/util.lua            # optional modules loaded with pd.require("lib/util")
  lib/arm64-v8a/
    libmy_mod.so            # optional native plugin and dependent ARM64 libraries
  protodroid-settings.ini   # created by ProtoDroid, not required in the ZIP
```

An API-2 package can combine every content type:

```ini
[mod]
schema = 1
api = 2
id = my.mod
name = My mod
version = 1.0
author = Your name
description = A short player-facing description.
depends = my.library, another.dependency
priority = 100

[content]
script = scripts/main.lua
script_api = 1
# files = files
# textures = textures.ini
# plugin = lib/arm64-v8a/libmy_mod.so
# plugin_api = 3
```

At least one content type is required. Paths must stay inside the package. Imports start disabled; enable/disable and option changes apply on the next Play.

- `schema` remains **1**. `api = 1` preserves ordinary file/texture/ABI-v1/v2 packages. Scripts, `depends`, and native ABI v3 require `api = 2` so an old launcher cannot silently ignore them.
- `depends` is a comma-separated list of up to 32 package IDs. Dependencies must be installed, valid, and enabled. ProtoDroid never auto-enables them.
- Dependencies initialize before dependants and shut down in reverse order. Priority chooses among packages whose dependencies are ready; higher priority still wins loose-file conflicts.
- Missing, disabled, incompatible, cyclic, duplicate, and self dependencies are rejected with a visible package error. The native launcher repeats validation even if folders are edited manually.
- Script paths use ASCII letters, digits, `/`, `.`, `_`, and `-`, and end in `.lua`.

## Lua entrypoints and modules

The entrypoint runs once before engine startup. Register hooks and events at top level, then return. A module has no extension in its require name and is resolved relative to the entrypoint's directory:

```lua
local util = pd.require("lib/util") -- scripts/lib/util.lua, cached per mod
assert(util.value == 42)
```

`pd.require` is available only during initial loading. Entrypoints and each module are text source, at most 1 MiB each and 2 MiB combined per mod. Parent traversal, absolute paths, symlinks escaping the package, NUL bytes, and Lua bytecode are rejected.

Every mod gets an independent Lua 5.4.8 state and these globals:

- `pd.api_version` (`1`) and `pd.mod_id`.
- `pd.log(...)` and `pd.warn(...)`, with bounded per-mod native logging.
- Base Lua functions plus `table`, `string`, `math`, and `utf8`. `print` uses ProtoDroid's log.
- No `io`, `os`, `package`, `debug`, native modules, `load`, `loadfile`, `dofile`, global `require`, or `string.dump`.

Each outer Lua callback has a 1,000,000-instruction budget. Each state has a 16 MiB Lua allocator limit, 64 callback registrations, and bounded event queues. An uncaught error or exhausted budget disables only that script, writes its `launch-error.txt`, and leaves other mods running.

**Trust boundary:** sandboxing blocks direct host file/OS access; it does not make engine calls or guest-memory writes safe. Use scripts from authors you trust. Wrong signatures, hooks, or addresses can still crash or corrupt the running game.

## Settings

The same author options work for Lua and native packages:

```lua
local enabled = pd.settings.get_boolean("enabled", true)
local amount = pd.settings.get_number("amount", 5.0)
local mode = pd.settings.get_string("mode", "Quiet")
```

Getters read only the current mod's `protodroid-settings.ini`. Missing or malformed values return the supplied default.

Declare options in the manifest:

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
default = Hello
max_length = 80
group = Diagnostics
order = 40
```

Up to 64 options are allowed. IDs use lowercase letters, digits, `.`, `_`, or `-`. Sliders require finite bounds/default and a positive step, with at most 10,000 whole steps. Choices save the choice text. Text is trimmed, single-line, and bounded. Settings writes are atomic and preserve unknown safe values.

## Engine symbols

ProtoDroid generates an index from all 105,037 records in `re/functions.csv`:

```lua
local symbol, err = pd.symbols.find("FUN_106dcfe0") -- ordinal defaults to 1
if symbol then
    pd.log(symbol.name, symbol.rva, symbol.address, symbol.ordinal, symbol.matches)
end

local second = pd.symbols.find("some::duplicate::name", 2)
local matches = pd.symbols.search("Camera", 32) -- case-insensitive substring, at most 64 results
local total = pd.symbols.count()
```

`rva` is relative to the original engine image base. `address` is the corresponding rebased guest address. Exact duplicate names use ascending-RVA, 1-based ordinals. Lookup returns `nil, error` when absent.

A name proves only that an inventory entry exists. It does **not** prove a signature, calling convention, object lifetime, or safe high-level behavior.

## Guest calls and memory

`pd.engine.call(target, arguments, ecx)` accepts an exact symbol, numeric RVA, or rebased engine address. It calls a recompiled entry on the engine thread and returns `eax`, or `nil, error` if no entry exists. `arguments` is an array of up to 16 unsigned 32-bit integers; `ecx` is optional for thiscall-style functions.

```lua
local eax, err = pd.engine.call(symbol.address, { 0x11, 0x22 }, 0x33)
```

Typed memory functions use absolute guest addresses and return `nil, error` for an invalid range:

- `read_u8/u16/u32`, `read_i8/i16/i32`, `read_f32`;
- `write_u8/u16/u32`, `write_i8/i16/i32`, `write_f32`;
- `read_bytes(address, 1..4096)` and `write_bytes(address, bytes)`;
- `read_string(address, optional_limit)` and `write_string(address, text)`;
- `is_valid(address, size)` for explicit range checks.

Integer writes are range-checked. Strings and byte accesses are bounded. Null and wraparound ranges are rejected.

## Hooks

Register function detours at top level. Later detours wrap earlier ones, including native-plugin detours:

```lua
local handle, err = pd.engine.hook("FUN_106dcfe0", function(ctx)
    local esp = ctx:get("esp")
    local arg0_bits = ctx:arg(0)
    ctx:set_arg(0, arg0_bits) -- replace the value to rewrite the argument

    local eax, edx = ctx:original() -- previous detour or real function, exactly once
    ctx:set("eax", eax)

    -- ctx:skip(optional_eax, optional_edx) intentionally bypasses the original.
    -- Returning without original()/skip() calls the original automatically.
end)
```

Context methods:

- `get(name)` / `set(name, u32)` for `eax`, `ecx`, `edx`, `ebx`, `esp`, `ebp`, `esi`, `edi`, `ret_target`, and `fs_base`;
- `arg(index)` / `set_arg(index, u32)` for stack arguments 0–63;
- `original()` returning `eax, edx`;
- `skip(optional_eax, optional_edx)`.

A saved context cannot be used after its callback returns. If a callback errors before the original ran, ProtoDroid invokes the original once as a fallback and disables that script. Hooks and callback storage remain valid through engine teardown; scripts are bypassed during late teardown.

## Lifecycle and custom events

Register callbacks at top level with `pd.on(name, callback)`:

```lua
pd.on("frame", function()
    -- Engine-thread callback; keep work bounded.
end)

pd.on("shutdown", function() pd.log("ordinary shutdown") end)

pd.on("my.mod.changed", function(value)
    pd.log("received", value)
end)

pd.emit("my.mod.changed", 42) -- queued for the next engine-frame event boundary
```

Custom events cross Lua states and accept up to eight unsigned 32-bit integer arguments. `frame` and `shutdown` cannot be emitted. Cascades and queues are bounded; events are not dispatched during shutdown. Native ABI-v3 plugins can queue the same script events with `emit_event`.

## Native ARM64 ABI

Include `modding/include/protodroid_plugin.h`. Existing ABI-v1/v2 plugins keep their exact host versions, structure prefixes, and behavior. ABI v2 adds per-mod settings; ABI v3 appends:

```c
uint32_t rva = 0;
if (!host->lookup_symbol(host->context, "FUN_106dcfe0", 1, &rva)) return -1;

uint32_t arguments[1] = { rva };
host->emit_event(host->context, "my.mod.native_ready", arguments, 1);
```

The resolved RVA can be passed to `guest_call` or `hook_engine`. Native plugins remain the right layer for performance-critical code, new host integrations, and evidence-backed renderer work. They are trusted native code.

The native example in `modding/example/` uses ABI v3, all four option types, named lookup, a real FOV detour, and script-event emission:

```sh
ANDROID_NDK_HOME=/path/to/ndk modding/example/build-package.sh
```

The source-only Lua example uses the same verified FOV target:

```sh
modding/script-example/build-package.sh
```

Import `build/protodroid-example-arm64.zip` or `build/protodroid-script-example.zip`, review, enable, then Play. Do not stack both FOV adjustments unless intended. Bundle native dependencies under `lib/arm64-v8a` with an `$ORIGIN`-relative runpath.

These are low-level building blocks, not claims that arbitrary entity spawning, new content types, or reflections already work. High-level gameplay objects and renderer features need their own proven signatures, lifetimes, resource contracts, and device validation.

## Texture packs and large projects

`textures.ini` maps ProtoDroid's versioned SHA-256 keys to same-format, same-dimension DDS files:

```ini
[hashes]
0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef = characters/alex.dds
```

Use **Dump original textures** in Setup to produce original-format DDS files and starter keys. These are not PPSSPP hashes.

- Supported formats: A8R8G8B8, X8R8G8B8, DXT1, DXT3, and DXT5, with bounded 2D mip chains.
- A replacement must match the original base dimensions and format. ProtoDroid does not resize textures or add rendering features through this path.
- Dynamic textures, render targets, cube maps, partial locks, and unsupported layouts are outside this contract.
- Missing replacement mips retain the original mip.
- Large packs can contain up to 65,536 ZIP entries/replacements within the importer's 4 GiB archive and 8 GiB extracted-byte quotas. Storage must cover the temporary archive and extracted copy.
- Replacement reads are lazy: only the requested mip is read temporarily. This is not a rendering-speed guarantee.

See `re/native-mod-platform.md` for texture and overlay internals. Host/CI tests do not prove every mod's behavior or appearance on a physical device.
