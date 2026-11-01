# ProtoDroid 0.15: AA correctness and mod-platform expansion

Status: implementation batches 1–3 are source-complete and host-tested; exact-commit ARM64 CI and physical-device AA verification are pending. Keep 0.15 unmerged and unreleased. Original game files remain read-only; probes use copies under `/tmp/protodroid-deep-re`.

Implemented evidence: the DXVK depth-resolve patch applies cleanly; generated symbols contain 105,037 records; the real Lua 5.4.8 runtime, dependency scanner, native ABI v1/v2/v3 fixtures, shipped Lua FOV example, and ASan/UBSan harness pass locally. No physical-device AA or third-party mod claim is made.

## Evidence and research

### Seen

- The supplied screenshots show severe world visibility loss with 4x MSAA and ordinary world rendering without AA. They do not identify a root cause by themselves.
- PCGamingWiki's Prototype page documents missing textures with AA, so the symptom is not invented or treated as a generic performance complaint.
- ProtoDroid's current device log identifies Mali-G720 MC7 and D24S8 auto depth/stencil. The log was captured with MSAA 0, so it is not an AA-run trace.
- DXVK 2.5.3 `D3D9DeviceEx::StretchRect` resolves multisampled depth using `VK_RESOLVE_MODE_AVERAGE_BIT`. Its own `ResolveZ` implementation quotes the D3D9 RESZ contract and uses sample zero. Newer DXVK also routes depth resolves through sample-zero-aware helpers. Depth is occlusion data, not color; averaging is not a valid Prototype depth-copy policy and can select unsupported mobile resolve paths.
- `re/functions.csv` contains 105,037 entries with substantial recovered gameplay, spawn, camera, health, render, and Lua-related names. The existing plugin API exposes only numeric RVAs and two events.
- Fabric separates loader, entrypoints, dependency metadata, events, and optional API libraries. REFramework and UE4SS demonstrate the usefulness of Lua plus native plugins and engine inspection. Lua 5.4.8 is small, MIT-licensed, Android-friendly, and can be embedded without OS/file/package/debug libraries.
- Existing ABI-v1/v2 plugins, overlays, texture packs, saves, controls, defaults, and the default-On menu-FPS behavior must remain compatible.

### Inferred

- Sample-zero depth resolves are a correctness fix and a strong candidate for the reported AA path, but host/static tests cannot prove the physical Mali visual result.
- Named symbol discovery plus scripts, hooks, events, and dependency-aware loading will make the platform substantially more capable without pretending every high-level game system has already been rebound.
- Independent sandboxed Lua states are safer and more deterministic than executing inside Prototype's insufficiently understood internal Lua VM. Engine Lua interoperability remains unknown until separately probed.

### Unknown

- Whether sample-zero resolves alone eliminate every missing-building case on the affected device.
- Every engine function signature, object lifetime, spawn prerequisite, and renderer requirement. No arbitrary `spawn_object` or reflection implementation will be fabricated from names.
- Physical-device behavior of new script hooks, renderer extensions, large packs, and third-party mods.

## Batch 1: AA correctness

1. Patch only DXVK's multisampled depth/stencil `StretchRect` resolve to use sample zero for depth and stencil, matching its RESZ path. Leave color resolves, sample counts, graphics quality, FIFO, shaders, and driver selection unchanged.
2. Add source-policy tests that distinguish the depth path from color resolve and require every Android patch to apply to pinned DXVK 2.5.3.
3. Require the real DXVK and ARM64 rebuild in exact-commit CI. Describe physical AA verification as pending until the affected device runs it.

## Batch 2: Fabric-inspired native/script platform

1. Preserve package schema/API 1 and native ABI v1/v2. Add optional script content without changing existing packages.
2. Add bounded script metadata, safe `.lua` paths, package dependencies, deterministic dependency-before-dependent ordering, cycle/missing-dependency errors, and visible launcher errors.
3. Generate a compact named-engine-symbol index from `re/functions.csv`. Support exact qualified lookup with duplicate ordinals and bounded substring discovery; do not infer signatures from names.
4. Embed pinned Lua 5.4.8 from source. One independent state per script package; no `io`, `os`, `package`, `debug`, native module loading, bytecode loading, or arbitrary file/network access.
5. Add allocator memory ceilings, per-callback instruction budgets, protected calls, persistent per-mod failure logs, and ordinary shutdown cleanup. Scripts execute on the engine thread and never block in unbounded host calls.
6. Expose a versioned script API for logging, per-mod settings, typed guest memory, named/numeric engine calls, function hooks with original-call control, frame/shutdown events, custom mod events, and bounded symbol discovery. Validate addresses, strings, argument counts, and callback registration.
7. Retain native ARM64 plugins for performance-critical or renderer-specific work. Append any new native ABI fields; never change v1/v2 structure layouts.

## Batch 3: usable author experience and validation

1. Supply an importable script example that demonstrates actual engine-symbol discovery, settings, events, guarded memory access, and a safe hook without claiming unproven gameplay outcomes.
2. Update the Mods library content labels and compatibility/load failures for scripts and dependencies. Preserve default-disabled import, enable/disable/remove, covers, options, and native trust confirmation.
3. Document script and native APIs, security/trust boundaries, symbolic lookup, duplicate handling, event ordering, examples, and the distinction between implemented primitives and future high-level game/render bindings.
4. Add host tests for manifest/dependency policy, symbol generation/lookup, Lua sandbox, memory limits, instruction limits, events, calls, hooks, error isolation, and ABI layout. Run sanitizer passes and full existing native/Java suites.
5. Build the real ARM64 `libmain.so`, native example, script example, DXVK, and signed APK in exact-commit CI. No dummy APK, merge, release, or claim of universal device/mod compatibility.

## Later high-value layers (not falsely claimed by this batch)

- Evidence-backed high-level player/entity/spawn/camera bindings with object lifetimes and signatures established by targeted probes.
- Renderer/device callbacks and custom render resources for trusted native plugins; true reflections require scene/depth/camera/render-pass work, not just a script loader.
- Data-driven content registries and additional language frontends only after their engine/resource contracts exist.
