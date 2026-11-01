# Native ARM64 mods

`include/protodroid_plugin.h` is the stable C ABI v1 for native plugins. It is not compatible with Windows x86 ASI/DLL binaries; existing plugins need source translation and an AArch64 rebuild.

The example detours `Camera::AdjustFOV`, adds five degrees with the same safety clamp as the built-in option, and registers a frame callback. Build its importable ZIP with:

```sh
ANDROID_NDK_HOME=/path/to/ndk modding/example/build-package.sh
```

Import the resulting `build/protodroid-example-arm64.zip` from the launcher Mods page. It installs disabled. Enable it only when you want the example's FOV change. Later detours wrap earlier ones, and a detour's original pointer calls the next detour in the chain.

Packages use `protodroid-mod.ini` as described in `re/native-mod-platform.md`. Bundle any dependent ARM64 shared libraries under `lib/arm64-v8a` and use an `$ORIGIN`-relative runpath when the dependency needs it. Texture keys use ProtoDroid's versioned SHA-256 contract, not PPSSPP hashes. Optional `dump_textures = 1` writes original-format top-level DDS files and `textures.ini` below `Prototype/texture-dump` (or the app-specific equivalent).
