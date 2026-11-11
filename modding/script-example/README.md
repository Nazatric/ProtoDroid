# Lua FOV example

Run `./modding/script-example/build-package.sh` from the repository root to create `build/protodroid-script-example.zip`. Import that ZIP in ProtoDroid's Mods screen, review it, enable it, then press Play.

The script resolves `FUN_106dcfe0`, installs a guarded detour, reads and writes the guest float argument, calls the original exactly once, consumes author settings, and registers frame/shutdown/custom-event callbacks. It is source-only; there is no native library inside the ZIP.

Do not stack it with the native FOV example unless you deliberately want both adjustments. Set one example's offset to zero or disable one package.

Another script can change its offset in tenths of a degree:

```lua
pd.emit("protodroid.example.lua_set_offset", 75) -- +7.5 degrees
```

The package requires the pinned Prototype engine hash. A generated name is not proof of another function's signature; research and test every new call or hook target.
