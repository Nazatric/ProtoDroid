# ProtoDroid 0.15 — implementation plan

This plan follows `re/protodroid-015-research-plan.md`. Changes are batched, tested locally, committed on `arena/78b126d7-protodroid`, and finally validated by the real ARM64 workflow on the exact candidate commit. Do not merge, create a PR, dispatch a release, or publish an APK.

## Batch A — focused live settings, launcher help, and updater

1. Add an explicit live-gameplay allowlist in `GameSettings`; retain the complete launcher setting list and existing defaults.
2. Reduce `TouchControls` to FPS/battery/menu pacing, FPS overlay, controls, and Layout. Remove restart state/UI and all restart-only graphics/mod/engine rows.
3. Add a native atomic menu-pacing override, initialized before engine/Bink startup, and update the effective FPS limit whenever FPS or battery saver changes. Preserve default-On menu unlock and FIFO presentation.
4. Add concise `GameSettings` help text and progressive Setup interaction for touch, mouse hover, and keyboard/D-pad focus. Include individual setting actions in the keyboard focus path. Keep full descriptions out of the page until requested.
5. Remove the Mods engine-fixes tab and its engine-setting actions/persistence; retain native/launcher fixes elsewhere.
6. Keep verified APK downloads, but make updater recovery open the exact fetched release page and use concise button/error copy.
7. Replace the opaque intro curtain with a narrow accent while leaving wallpaper visible immediately. Add API-appropriate immersive status/navigation hiding with transient swipe reveal to launcher activities.

Validation: Java parser checks, host settings/update tests, source-policy tests, and a clean review that no native rendering, save, FIFO, or driver defaults changed.

## Batch B — genuine mod configuration and large-pack support

1. Implement an independent schema parser for `[setting.<id>]` entries in `protodroid-mod.ini`:
   - `toggle`: On/Off with a default;
   - `slider`: finite min/max/default/step;
   - `choice`: bounded named choices;
   - `text`: bounded single-line UTF-8.
   Enforce bounded counts/lengths, unique IDs/choices, safe persistence keys, and clear errors.
2. Add `ModConfig` value loading/normalization/atomic saving in each mod folder. Preserve unknown safe values, reject symlink/path escapes, and never write original game data.
3. Add a styled, landscape-friendly `ModConfigActivity`: two-column option cards where space permits, accessible native controls, Reset/Done/Back, short shared help on hover/focus/touch, reduced-motion entry, and save/error feedback.
4. Replace Mods' three ordinary card actions with Details/Configure (when declared), Enable/Disable, and Remove without crowding narrow screens. Keep import, covers, descriptions, priorities, active state, plugin confirmation, and removal confirmation.
5. Make library scans shallow and deep validation mandatory on import/enable/launch preparation. Raise archive/extracted/entry/mapping budgets with actual streamed-byte checks and host boundary tests.
6. Extend the native plugin ABI compatibly:
   - package API remains schema 1;
   - `[content] plugin_api=2` opts into ABI v2;
   - v1 receives its original host version/structure size;
   - v2 appends per-mod boolean/number/string getters using `PdHostApi.context`.
7. Change DDS replacement caching to header/offset metadata plus lazy level reads into the locked destination, retaining format/dimension/mip validation and safe paths. Do not add new texture formats or resize/upscale behavior.
8. Update the example plugin/package and `modding/README.md` with a complete configurable example and honest limits. Keep README English-only and concise.

Validation: host Java tests for schema, normalization, atomic persistence, compatibility rejection, and large boundaries; native host tests for ABI sizing/config lookup/DDS offsets; existing mod security and package tests; exact ARM64 compilation later.

## Batch C — diagnostics, issue forms, release hygiene, and metadata

1. Reduce duplicate durable tour journal/marker work while retaining atomic phase state and immediately durable failures/results. Add elapsed time, exact executed-action counts, gameplay dispatches, Presented frames, and timing snapshots per phase. Preserve automatic gameplay entry, all sequential phases, shadow failures, isolated saves/logs/cache, and explicit non-visual claims.
2. Simplify all GitHub issue forms: remove emojis, combine repeated device questions, shorten instructions, and retain essential version/device/problem/log evidence.
3. Fix release workflow dispatch parity so protected signing is selected for both explicit dispatch types when publishing without disposable approval. Keep ordinary pushes and CI artifacts disposable/non-release-eligible.
4. Separate explicit `allow_certificate_change` from `allow_ephemeral_release`: a one-time transition to a new **persistent** certificate must not require permitting disposable releases. Require exact version/source/signing checks as before. Do not claim protected secrets are configured while GitHub returns 403.
5. Bump once to version 0.15/versionCode 15; add a concise 0.15 changelog and README/modding updates. Record device-validation and signing limitations honestly.

Validation: all host/Python/shell/Java tests, YAML parsing, signing/workflow source tests, clean `git diff --check`, then push the exact candidate commit and run the ordinary real ARM64 CI workflow. Inspect APK/package/signing provenance and job results. No release or merge.

## Completion criteria

- The gear panel contains only safe live options plus Layout; no restart-only graphics or mod/engine experiments.
- Mods contains no engine settings, supports easy existing imports and author-defined per-mod controls, and retains ABI-v1 plugins.
- Large texture packs have practical bounded imports and lazy native replacement reads without broadened rendering promises.
- Launcher artwork is visible during intro, system bars use immersive transient behavior, and explanations are progressive/concise.
- Download/install verification remains intact; the release workflow cannot silently turn a persistent repository-dispatch release into a disposable one.
- Tour coverage is not weakened and failures remain detailed/durable.
- Exact-commit ARM64 CI is green. Any physical-device or protected-secret step not performed is explicitly listed as unverified, not implied by CI.
