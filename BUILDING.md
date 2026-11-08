# Building this APK

This zip contains only what the build consumes. No build-output binaries.

1. `build-scripts/toolchain_rebuild.sh` (downloads NDK r28c + cmdline-tools), then
   `build-scripts/rebuild_stage_a2.sh` (JDK 17 — Adoptium API or the Azul CDN fallback,
   SDK packages, glslang, then the dependency build: Dobby, SDL2 2.30.9, FAudio, FFmpeg
   [Bink only], DXVK 2.5.3 with the patches in `deps-recipe/android/patches/` — the Vulkan
   1.1/1.2 KHR/EXT compatibility ladder, Mali pipeline-compile fixes, Adreno
   unified-memory backport).
2. Stage the four release libraries (libSDL2, libFAudio, libvulkan_wrapper, libdxvk_d3d9)
   into `$HOME/protodroid-data/android-deps/arm64-v8a/lib/` — extract them from the
   official ProtoDroid release APK (`lib/arm64-v8a/`); their sha256 values are embedded in
   `build-scripts/build_v016.sh`, which fails the build if they differ. The build replaces
   libmain only; everything else ships the original release bytes.
3. `build-scripts/build_v016.sh`: compiles libmain (the recompiled engine + the Win32
   platform layer), verifies the stamps and source contracts, packages the APK with the
   bundled minimap mod, signs it, and verifies the release libraries are byte-identical.

What is what:
- `build/recomp/native/` holds the generated engine recompilation (`part_*.c`, `table.c`)
  referenced by `src/native/Makefile` — required to link libmain, already generated.
- Regenerating it from scratch additionally needs `re/functions.csv` and
  `prototypeenginef.dll` (the game binary) — both excluded here as data/binaries; the
  generator is `tools/recomp/recomp.py`
  (`recomp.py --functions all --base 0x7c000000 --split 400 --out build/recomp/native`).
- `src/native/mod_symbols.inc` is generated but required by the build; included.
- The minimap mod source lives in `modding/minimap/package`; `android/assets/mods/` holds
  the packaged zip that the app auto-installs on first run.
