# Adreno Gen8 Turnip patches

These patches are applied only to the pinned `whitebelyash/mesa-unified` `turnip/gen8` source used for `libvulkan_freedreno_gen8.so`.

Source: `ducvd89/Bloodborne-Android` at commit `856c823f37e3abf411df565e2fb0c11ea3993a39`, under `tools/android/mesa-patches/`. The corresponding driver release identifies the Mesa base as `whitebelyash/mesa-unified` commit `9c7e022677dfa3abb356c2b6732cbd2e25783d01`.

- `0001`–`0004`: KGSL fence/timestamp wait handling, including overflow, error propagation, zero-timeout behavior, and timeout rounding.
- `0010`: resolve Android's `hw_get_module` lazily so the driver does not acquire a hard `libhardware` dependency.

No emulator-specific rendering, performance, frame-generation, or X11 patches are included. Mesa's existing license and the individual patch headers apply.
