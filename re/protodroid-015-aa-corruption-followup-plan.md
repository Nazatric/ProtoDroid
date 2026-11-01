# ProtoDroid 0.15: AA corruption follow-up

Status: exact-commit ARM64 CI passed; physical visual verification remains pending. Keep 0.15 unmerged and unreleased. Original game files and the `Logs`/`TestingFiles` branches remain read-only. This batch does not claim physical-device visual verification.

## Evidence inventory

### Seen

- The user reports that see-through/missing-building corruption with Prototype AA still occurs in the newest 0.15 build, despite the existing sample-zero depth/stencil resolve correction.
- The supplied screenshots establish the visual symptom, but not whether the engine, D3D9 bridge, DXVK, or driver is responsible.
- PCGamingWiki's Prototype page and the Prototype community troubleshooting guide both document missing textures with the game's AA and recommend disabling it. Thus built-in MSAA is itself a known problem path, not merely a ProtoDroid performance setting.
- The newest `Logs/proto-native.log` is exact source `af7c7e5`, Mali-G720 MC7, Android 16, Auto/direct Vulkan 1.3. It records `game_aa_level = 0`, CreateDevice/Reset `msaa 0/0`, and a completed scripted tour. It is not an AA-enabled reproduction and explicitly says visual verification was not performed.
- The `(1)` logs are 0.14/Adreno 840 and the current `TestingFiles` logs are 0.14/Mali-G57 MC2. They also use MSAA 0. They must not be presented as exact-0.15 AA evidence.
- The latest tours complete without a recorded crash. `TestingFiles/result.prev.txt` is an older failed wait, not the latest result. Startup open probes, unsupported CPU-frequency reporting, and isolated audio underruns do not establish additional defects suitable for speculative rewrites.
- Medium and High launcher presets currently select the game's own 2x/4x MSAA. Existing saved preset values would survive an app update unless deliberately migrated.
- DXVK 2.5.3 already has a final `DxvkSwapchainBlitter` draw for single-sampled and multisampled presentation. This is a bounded integration point that does not require modifying recovered game shaders or texture resources.
- Timothy Lottes' FXAA documentation describes a single full-screen, display-referred luma-edge filter. A separate DXVK fork's present-pass FXAA implementation provides a useful reference, including gamma-space sampling and single-sample gating, but is not evidence that Prototype is fixed on the user's Mali device.

### Inferred

- Avoiding Prototype's built-in MSAA while applying FXAA to the completed frame is a practical engine-level compatibility path: it can smooth edges without invoking the MSAA/depth/effects combination associated with missing textures.
- The present blitter can implement this in one pass for ProtoDroid because it has no additional FSR/bloom chain. A second intermediate image is unnecessary here.
- Compatibility mode must keep GameDB AA at zero on subsequent reads and normalize D3D9 present MSAA at CreateDevice/Reset/additional-swapchain boundaries. A one-time launcher write alone would let the in-game Video menu re-enable the broken path.
- Exact old Medium/High preset values can be migrated once to FXAA. Custom AA choices must remain untouched, and original 2x/4x MSAA must remain available for comparison.

### Unknown

- Whether FXAA eliminates every instance of the user's corruption on the physical Mali-G720 device. CI cannot establish visual correctness.
- The original engine's complete MSAA failure mechanism. No arbitrary renderer, shader, depth, or texture rewrite is justified by the supplied logs.
- FXAA's exact frame-time cost and text softening on each device. It is post-process AA, not equivalent to geometric 2x/4x MSAA.
- Whether the older Adreno/Mali-G57 issue reports share this mechanism. They have no AA-enabled trace and must remain separate reports.

## Implementation batch

1. **Append a real FXAA choice without breaking saved indices.** Keep `game_aa_level` values `-1/0/1/2` and their preference positions unchanged; append value `3` as FXAA. Explain that it avoids built-in MSAA and applies on the next Play. Change Medium/High presets to FXAA, leaving Low at Off. Migrate only exact old Medium/High preset AA indices; do not rewrite Custom or explicit MSAA selections.
2. **Enforce the selected mode in native policy.** Map FXAA's launcher value to GameDB AA 0. Unlike ordinary one-shot launcher overrides, reapply that mapping on later AA reads with recursion protection, so in-game setting changes cannot silently reactivate MSAA. Leave Shadow Detail, Graphic Quality, Game default, ordinary Off/2x/4x behavior, and their one-shot semantics unchanged.
3. **Guard D3D9 presentation boundaries.** In FXAA mode only, normalize present-parameter multisample type/quality to none/zero for CreateDevice, Reset, and additional swapchains. Append `d3d9.forceSwapchainMSAA = 0` after user configuration because DXVK otherwise applies that override after application normalization. Log requested and effective values plus the selected policy. Do not globally alter multisampled render-target/depth creation or other D3D9 formats; ordinary MSAA leaves custom DXVK configuration untouched.
4. **Add an opt-in DXVK present shader.** Set a dedicated ProtoDroid FXAA environment flag before DXVK initialization only for the new mode. Add a new fragment shader to the existing swapchain blitter; leave the existing copy/blit/MSAA shaders unchanged. Select it only for the main single-sample, non-blended present when FXAA was requested. Keep cursor composition, gamma/color conversion, source/destination rectangles, scaling, and multisampled presentation on their original paths.
5. **Use display-referred, bounded sampling.** Compute luma from gamma-encoded local samples, early-out on flat regions, blend along the clamped edge direction, clamp every integer fetch to `srcRect`, and convert back into DXVK's existing input/output color transform. Preserve source alpha and avoid reading or writing the same image. Use one final pass rather than an intermediate render target.
6. **Instrument rather than overclaim.** Log native FXAA selection, GameDB mapping, requested/effective present MSAA, and DXVK shader activation. Keep the existing tour's `visual_verification=not-performed` semantics. A future device run must inspect buildings with FXAA active and compare against Off and original MSAA.
7. **Validate in layers.** Add host tests for the Java preset migration and native AA mapping/present policy; extend GameDB tests to prove repeated FXAA enforcement and unchanged one-shot ordinary behavior. Add patched-source tests for shader selection/gating and ensure every DXVK patch still applies in filename order. Compile the new GLSL to SPIR-V and run source/sanitizer suites locally where possible. Require exact-commit real ARM64 CI, including embedded-DXVK verification and signed APK packaging. No dummy APK, merge, release, universal-AA, or FPS claim.

## Local validation completed

- Native AA policy and GameDB tests pass under UBSan: original values, FXAA→0, repeated reads, setter recursion, one-shot ordinary MSAA, present samples, and final DXVK forced-MSAA override.
- The ordered Android DXVK patch chain applies from a fresh v2.5.3 clone; the earlier unified-memory and sample-zero depth/stencil tests still pass.
- The new FXAA GLSL and DXVK's original present shader both compiled to SPIR-V with the npm glslang fallback. The fresh-patch FXAA source produced the same 4,518-word shader; exact CI will also compile it with `glslangValidator`.
- Structural FXAA gating/fallback tests, compatibility source tests, workflow YAML parsing, and Java syntax parsing pass. The pure Java migration test is wired into CI because no local `javac` is available.
- Existing graphics, mod feature/texture/hook/platform/file/plugin, Lua, save-path, state-cache, performance-hint, CPU-affinity, present-stats, diagnostic-tour, diagnostics, GPU-profile, driver-recovery, wrapper, release, updater, and issue-template checks pass locally.

## Exact-commit CI validation

Source commit `f0eb5bcc64d1474f8c901ade42763497d501f8dd` passed [build-apk run 41348438974](https://github.com/Nazatric/ProtoDroid/actions/runs/41348438974). This included the new host AA/GameDB tests, Java preset migration test, `glslangValidator` shader compilation, all ordered DXVK patches, the real ARM64 dependency/native/plugin/Lua builds, Android Java compilation, signed APK packaging, and embedded-DXVK verification. The release job was skipped; no pull request or release was created.

Artifacts: `prototype-apk` **13100513846**, `debug-symbols` **13100727917**, `make-logs` **13100533754**, `android-deps-arm64-v8a` **13100422035**, `protodroid-mod-example` **13101241845**, and `android-turnip-arm64-v8a` **13099938801**. These are CI artifacts, not a 0.15 release. CI does not establish that the physical Mali corruption is gone.

## Explicitly out of scope without new evidence

- DXVK upgrades, driver-version guessing, global sample-count rewrites, game shader edits, texture-format changes, shadow filtering changes, or forcing graphics quality down.
- Scheduler/core pinning, audio buffering, memory limits, cache policy, or I/O changes based on the completed tours.
- Claims that issues #20, #22, or #28 are fixed by this AA path. Their versions, GPUs, and traces do not prove a common cause.

## Physical acceptance procedure (still required)

1. Install the exact CI build and select **Anti-Alias Level → FXAA**, then relaunch Play.
2. Confirm the native log shows FXAA policy, GameDB AA 0, effective present MSAA 0/0, and DXVK FXAA activation.
3. Revisit the same building/view that became transparent. Compare FXAA, Off, and original MSAA after restarting between modes.
4. Check ordinary world geometry, character/building shadows, HUD/text, Bink video, save/load, and rotation. Record screenshots plus native/DXVK logs.
5. Treat the source fix as device-unverified until those results exist. If corruption persists with effective MSAA 0/0, stop attributing it to built-in MSAA and capture a focused render trace rather than stacking more speculative patches.
