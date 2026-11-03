# ProtoDroid native, recompilation, GPU, and modding research

Status: research and 0.15 implementation record, 2026-11-03. Original game files remain read-only. The original pre-WebView/pre-Compose native launcher is restored; both rejected replacement presentations are removed. The follow-up BC1 optimization has host A/B measurements, with ARM64 CI and controlled physical-device comparisons tracked separately. Version 0.15 remains unmerged, untagged, and unreleased.

## 1. Evidence rules and current baseline

This document uses three labels:

- **Verified:** backed by a primary specification, upstream project documentation, or this checkout.
- **Implication:** a reasoned ProtoDroid design or investigation recommendation, not an observed performance result.
- **Device-unknown:** requires a physical device, representative workload, driver trace, or visual comparison. CI and host tests cannot settle it.

Current checkout facts are documented in [README.md](../README.md), [re/native-mod-platform.md](native-mod-platform.md), and [re/protodroid-015-compatibility-stability-plan.md](protodroid-015-compatibility-stability-plan.md):

- ProtoDroid statically translates the supplied 32-bit x86 engine to generated C/C++ and compiles it for AArch64. It does not use Wine or a full x86 emulator.
- The runtime supplies guest memory, x86-like ordering, threads, fibers, Win32/CRT behavior, and imports. D3D9 is bridged to the project's pinned DXVK 2.5.3/Vulkan path.
- Bink, XAudio2, input, saves, presentation, driver selection, and mods already have native implementations and regression coverage.
- Mods include data overlays, keyed same-format DDS replacements, restricted Lua, and independently built ARM64 plugins. Windows x86 ASI/DLL plugins are not loaded.
- Physical-device FPS, input latency, RSS, thermal, visual, and broad Mali/Adreno/PowerVR compatibility remain unproven. Existing logs establish particular events, not universal bottlenecks.

The correct optimization unit is therefore **generated code + runtime + imports + graphics translation + Android scheduling**, not the recompiler in isolation.

## 2. Static recompilation, native ports, interpreters, and JITs

### Verified

| Approach | What executes | Main cost/risk | Useful lesson |
| --- | --- | --- | --- |
| Source port/decompilation | Reconstructed native source | Large reverse-engineering and behavior-preservation effort | Best long-term semantic control when source is reliable |
| Static recompilation | Ahead-of-time translation of known guest code | Function discovery, indirect control flow, ABI/runtime fidelity, code size | Lets a host compiler optimize while retaining original machine-code behavior |
| JIT/dynamic recompilation | Host code produced and cached at runtime | Warm-up, block lookup, invalidation, executable-memory management | Handles dynamic/unknown code more naturally; caches need correctness keys |
| Interpreter | Guest instructions decoded and executed individually | Dispatch cost | Valuable as a semantic oracle and fallback for differential tests |
| Full compatibility/emulation stack | Guest code plus broader OS/runtime environment | More translated code and environmental surface | Useful comparison, not a drop-in optimization for an existing static port |

XenonRecomp explicitly states that its generated C++ is not useful without a runtime; it also documents instruction-specific floating-point behavior, indirect mapping, and ARM64 SIMD support through SIMDe [1](https://github.com/hedge-dev/XenonRecomp). N64Recomp similarly translates functions into C, relies on metadata, handles delay slots and jump tables, and expects a separate runtime [2](https://github.com/N64Recomp/N64Recomp).

FEX and Box64 show the other side of the trade-off: dynamic translation must manage code blocks, caching/invalidation, flags, memory ordering, and native/guest transitions. Box64's own options expose correctness/performance choices for strong memory ordering, safe flags, block size, rounding, and NaNs rather than presenting one universally safe “fast” preset [3](https://raw.githubusercontent.com/ptitSeb/box64/main/docs/USAGE.md).

### Implication for ProtoDroid

- Keep static recompilation. Replacing it with FEX, Box64, Dynarmic, Wine, or a new JIT would be a wholesale architecture change, not an evidence-based optimization.
- Treat unresolved imports, unknown indirect targets, callbacks, fibers, and exceptional control flow as correctness work before micro-optimization.
- Maintain a small scalar/reference path for generated helpers and translated operations where practical. Differential tests are more valuable than checking only that generated code compiles.
- Any future cache for generated code or mod code must be derived, versioned, invalidateable data. Include guest-binary identity, translator/ABI version, semantic options, and CPU feature set in its key. Never trust executable cache content from shared storage.
- Direct generated calls do not automatically pass through a central dispatcher. Function-level hooks must use the established detour mechanism; adding a hook only to dispatch would miss direct calls.

## 3. x86 to ARM64 correctness contract

### 3.1 ABI, data layout, and control flow

**Verified:** Android's ABI documentation covers instruction set, endianness, calling conventions, alignment, and ELF layout; Android is little-endian and `arm64-v8a` has an Armv8.0 baseline [4](https://developer.android.com/ndk/guides/abis). AAPCS64 requires 16-byte stack alignment at public interfaces and defines register preservation and argument passing [5](https://github.com/ARM-software/abi-aa/blob/2bcab1e3b22d55170c563c3c7940134089176746/aapcs64/aapcs64.rst).

Important boundary rules include:

- x0–x7 carry integer arguments/results; x8 has an indirect-result role; x19–x28 are callee-saved.
- x16/x17 are linker/intra-procedure scratch registers, not guest storage.
- x18 is platform-defined. Follow the Android platform ABI/compiler rather than appropriating it for guest state.
- The low 64 bits of v8–v15 are callee-saved under the base AAPCS64 rules; do not preserve or clobber vector registers based on x86 intuition.
- Guest x86 stack arguments, `ret N` callee-pop conventions, variadic CRT calls, callbacks, and host AAPCS64 calls are different contracts. Thunks must marshal them explicitly.
- Windows LLP64 and Android LP64 type sizes differ. Guest structures need fixed-width types and explicit layout, not host `long`, pointer, or natural-alignment assumptions.
- A 32-bit guest pointer is not a host pointer. Preserve 32-bit wrap/zero-extension rules and validate the guest range before forming a host address.
- Windows SEH/unwind information is not interchangeable with Android ELF unwinding. Dynamically generated code on Windows needs explicit function/unwind registration; the same general lesson applies to any future runtime-generated ARM64 code [6](https://learn.microsoft.com/en-us/cpp/build/arm64-windows-abi-conventions?view=msvc-170).

**Implication:** optimize a thunk only after proving its exact guest signature, stack cleanup, register liveness, callback behavior, and failure path. A faster generic thunk cannot compensate for one wrong variadic or callback signature.

### 3.2 Memory ordering and atomics

**Verified:** Arm is weakly ordered and provides acquire/release and optional atomic extensions with different semantics and feature requirements [7](https://documentation-service.arm.com/static/62a304f231ea212bb662321d). x86-TSO is stronger than ordinary Arm ordering but is not sequential consistency. FEX's technical discussion demonstrates why translated ordinary accesses, unaligned/split atomics, and write-combined memory are separate problems [8](https://fex-emu.com/Scourge-of-emulation/).

Do not reduce this to “x86 is strong, ARM is weak, so remove every fence”:

- Ordinary x86 loads/stores, locked operations, unaligned accesses, and cache-line-crossing atomics have different requirements.
- Naturally aligned Arm single-copy atomicity does not prove every unaligned or multi-byte guest operation cannot tear.
- Acquire/release, LSE, LSE2, and LRCPC-family instructions are not interchangeable and are not all present on the Armv8.0 Android baseline.
- C/C++ `volatile` is not a cross-thread atomic or memory-ordering contract.
- Compiler ordering and hardware ordering both matter. A source-level barrier does not repair undefined behavior in a racy host implementation.

**Implication:** retain the runtime's x86-like ordering. Relax only a proven hot access or communication protocol with a semantic proof, stress tests, and representative multi-core device testing. Do not add a global TSO-off, `STRONGMEM=0`, or equivalent setting by copying emulator presets.

### 3.3 Floating point, flags, and SIMD

**Verified:** Arm's SSE-to-Neon guidance recommends getting a correct port first, then profiling and replacing problematic mappings. Basic arithmetic often maps well; shuffles, movemasks, horizontal operations, specialized operations, precision, division, square root, and rounding can require different algorithms [9](https://support.arm.com/documentation/102581/0200/Port-with-SSE2Neon-and-SIMDe).

**Implication:**

- Preserve NaN behavior, signed zero, rounding, denormal/flush behavior, exception semantics where observed, and x87 precision requirements. Do not enable fast-math globally.
- Treat SIMDe/SSE2NEON as portability tools, not proof of optimal code. Inspect emitted AArch64 and compare results against scalar/x86 oracles, including edge cases.
- Advanced SIMD is baseline on AArch64; dot-product, FP16, SVE/SVE2, and later features are not. Use compile-time baseline code plus detected dispatch for optional features.
- Revisit flag optimization only from liveness and emitted-code evidence. The earlier masked-flag draft showed no meaningful synthetic gain and should not be revived without representative ARM64 profiling.

## 4. Runtime and import architecture

### Verified

Static recompilation removes runtime instruction discovery/translation, but it does not remove:

- guest-memory access and address translation;
- guest/host stack and ABI transitions;
- OS, CRT, audio, video, input, and D3D imports;
- indirect function mapping and hook boundaries;
- synchronization and object lifetime;
- cache behavior in a large generated binary.

DXVK's own documentation also shows that a translation layer has observable CPU work: shader/pipeline compilation, fixed-function shader generation, submissions, draws/render passes, descriptor work, and memory allocation can all be inspected through its HUD [10](https://raw.githubusercontent.com/doitsujin/dxvk/v2.5.3/README.md).

### REA 6.8.0 shipped-artifact audit

**Scope and provenance:** the official `morluto/rea` project was used through its local CLI, not a similarly named repository or an unreviewed installer. The inspected source was commit `2cb29693c3d574c4898832047fe309ac1ad00e9d`; the executed npm package was `rea-agents@6.8.0` on Node `22.23.4`. REA's built-in PE inspections and caller-supplied GNU objdump provider were used according to its Evidence and provider-boundary documentation [37](https://github.com/morluto/rea/tree/2cb29693c3d574c4898832047fe309ac1ad00e9d). No agent configuration was registered, no deep-analysis provider was installed, and no target was executed.

Analysis used read-only copies exported from immutable `TestingFiles` commit `b063714169100ba4468d6754b1c1552e3e6b2632`. The engine copy's SHA-256 was `d331be205bdde99897952ee88261ab7e63c5c65e36a4660e2a367886a3537071` before and after analysis; every REA objdump result also reported `artifact_changed: false`. Ghidra, IDA, Hopper, JADX, and adb were unavailable, so none of these results is pseudocode, decompiler coverage, or device/runtime confirmation.

The bounded command sequence was:

```sh
rea inspect-pe-imports <engine-copy> --format json
rea inspect-pe-load-config <engine-copy> --format json
rea inspect-with-objdump <engine-copy> file_headers --format json
rea inspect-with-objdump <engine-copy> section_headers --format json
rea inspect-with-objdump <engine-copy> symbols --format json
```

**Verified from the shipped engine:**

- REA Evidence `ev_0175f1a2cb6ffa511f30ac35c4bc7ad67ebe763a8dcf557bbb52400b5e449956` decoded the complete standard import table: 20,205,568-byte PE32/x86 image, **17 DLLs and 368 imports**, no delay-import directory, no parser issues, and pefile-compatible imphash `75cd944d0ed3cc1d2093c5bd866c18a1`.
- GNU objdump Evidence `ev_9f6b01d3eedf63cc2cc5fdc98f840f182edf9c20967ad875674cf7e7c6135ec4` identified `pei-i386` and entry `0x10a888c5`. Section Evidence `ev_792be6f7cd3f2709d5b5c3318b9aabbe98268ce472e0eebeb8b64c6ac2bb35b6` measured `.text` at RVA `0x1000`, VMA `0x10001000`, size `0x00d8e364`. Symbol Evidence `ev_31528ebe6c09c0aa5b1eea0c7c60750d93b6980ef84ca3bdbe288e31654a4dac` returned `no symbols`; the existing Ghidra-derived `re/functions.csv` therefore remains valuable for targeted boundaries.
- Load-config Evidence `ev_9607492e92ef56fa93f080e9fdf8652bd34f39ff2f3841c21156e53c70e52dce` observed no TLS directory, a security-cookie declaration, and one SafeSEH table entry; CFG pointers/flags were absent from the fields read. These are PE declarations, not observations of callback execution or mitigation enforcement.
- As a separate inventory check, `prototypef.exe` SHA-256 `6d5b92b9dfcdf5edfdee9b63c1eb8a3ac8369ca5622b71fc2e44c11bc21fd548` had 102 imports across seven DLLs (Evidence `ev_e5bd331273b9a8f282f462eb8718cf32a8bf6a60b7a325bedd62bac9a54e730f`). It was not used to infer the DLL host's handler coverage.

**Source-derived handler comparison:** normalizing DLL case and preserving exact names/`#ordinal` keys accounted for all 368 engine imports as follows:

| Classification | Count | Boundary |
| --- | ---: | --- |
| Exact `ImportDef` registration found | 364 | Registration exists; signature and runtime behavior are separate questions |
| Intentional non-ordinary paths | 4 | `_HUGE`/`_adjust_fdiv` data slots and recompiler-special `_setjmp3`/`longjmp` |
| No exact ordinary handler key | 0 | Every shipped-engine import key has an explicit ordinary or special path |

The nine `OLEAUT32.dll` ordinal keys now resolve to the same existing handlers as their verified named exports: `#2`/`SysAllocString`, `#4`/`SysAllocStringLen`, `#6`/`SysFreeString`, `#8`/`VariantInit`, `#9`/`VariantClear`, `#12`/`VariantChangeType`, `#200`/`GetErrorInfo`, `#201`/`SetErrorInfo`, and `#202`/`CreateErrorInfo`. Production named/ordinal dispatch tests verify identical return and stdcall cleanup behavior. This repairs the ordinal-registration mismatch only; it does not claim broader COM or online-service support.

The final 17 keys are now explicit too. Wine's [WS2_32 export specification](https://raw.githubusercontent.com/wine-mirror/wine/master/dlls/ws2_32/ws2_32.spec) maps the 16 socket ordinals to `accept`, `bind`, `connect`, `getpeername`, `getsockname`, `getsockopt`, `ioctlsocket`, `listen`, `recv`, `recvfrom`, `select`, `send`, `sendto`, `setsockopt`, `shutdown`, and `__WSAFDIsSet`. Because online services remain unported, the connection operations return their documented failure values and retain the existing `WSAENETDOWN` policy; `__WSAFDIsSet` performs a real bounded 64-entry `fd_set` membership check. Wine's [DSOUND specification](https://raw.githubusercontent.com/wine-mirror/wine/master/dlls/dsound/dsound.spec) maps `#2` to `DirectSoundEnumerateA`. The Android host exposes no DirectSound device, so valid enumeration returns `DS_OK` without invoking the callback and a null callback returns `E_INVALIDARG`; this does not add or claim a DirectSound renderer. Production named/ordinal dispatch tests cover failure values, empty enumeration, null/match/miss/bounded set behavior, and exact stdcall cleanup.

`ImportThunk` still retains a null handler if a future import has no exact registration, and `DispatchImport` raises the detailed fatal error only if that slot is called. The current shipped-engine ledger has no such unresolved key.

A bounded byte-reference probe, followed by function-entry objdump disassembly for the examples below, confirmed that these imports were not merely unused import-table metadata:

- `0x1093eb10` in `FUN_1093eab0` calls `WS2_32!#20` through IAT VA `0x113322c8`.
- `0x1085f6fa` in `FUN_1085f6f0` calls `OLEAUT32!#6` through `0x113320b4`; `0x10a89490` in `_com_issue_errorex` calls `OLEAUT32!#200` through `0x113320c8`.
- The `DSOUND!#2` thunk at `0x10ba2be8` jumps through `0x11331ba0`; a direct call at `0x10ac18ee` lies in recovered `audio::DirectSoundRendererDriverModel::vfunction1`.

These are static code references only. They do not establish that Android gameplay reaches them, that the current native audio/network paths select them, or that they explain any existing device failure.

**Bounded timer-route check for the 0.15 tour:** the PE import table and bounded GNU objdump ranges were rechecked against the hashed engine copy. The relevant IAT VAs are `GetTickCount` `0x11331bd4`, `timeGetTime` `0x11332284`, `QueryPerformanceCounter` `0x11331cb0`, and `_RADTimerRead@0` `0x113323b8`. Direct call sites include:

- `0x10a8f80b` (`FUN_10a8f80b`) calls GetTickCount and clears EDX before returning; `0x10a891f5` in `___security_init_cookie` is a separate startup call.
- `0x10a71ab0` (`FUN_10a71ab0`) calls timeGetTime and multiplies the result by 1000. Recovered code has direct callers of this wrapper.
- `0x1063c837`, `0x1063ce57`, and `0x10a71ad7` call QPC directly in small recovered timing wrappers; other direct QPC sites also exist.
- RADTimerRead's import thunk is `0x10a8a042`. A direct-call scan found six candidate call sites, including `0x105a1af2` in recovered `movie::BinkCallback::vfunction4_for_IFileCompletionCallback`; bounded disassembly confirms that site and `0x105a14fc`, including subsequent elapsed-value bookkeeping.

The export table independently places `EngineService` at `0x1085eb20`; no timer behavior is inferred merely from that export. These results verify that code routes for the selected timer imports exist. They do **not** prove which routes a particular gameplay session executes, that 4× guest time increases player velocity, that climbing succeeds, or that Bink audio/video remains synchronized. The tour's automatic import profile and device results are needed for those questions.

**Implication:** keep this import-coverage ledger and the production dispatch tests synchronized with the exact PE import inventory, including explicit exceptions for data imports and recompiler-special control flow. Before expanding networking, COM-variant, or DirectSound behavior, verify ordinal-to-export mappings and implement or alias only semantics covered by tests. Do not replace the detailed fatal path with unconditional success stubs. This is a correctness companion to—not a substitute for—the import profiling below.

### Implication: measure crossings before rewriting them

The existing `import_profile = 1` reports guest and Win32/D3D9 wall time per thread. Use that first with a repeatable scene:

1. Record total wall time, process CPU, per-thread guest time, and per-thread import time.
2. Identify whether a stall is actually in an import, guest code, presentation, file I/O, or waiting on another thread.
3. Only if the data supports it, add low-overhead per-import call counts/time for the small candidate set.
4. Specialize only hot, fully understood imports. Keep the generic path for unknown signatures and uncommon calls.
5. Compare generated AArch64, correctness tests, and sustained frame/CPU results. Reject changes that merely improve a synthetic thunk loop but regress code size or real gameplay.

Good candidates are likely to be frequent, narrow, leaf-like helpers with stable signatures. D3D9 calls that block, allocate, synchronize, or invoke callbacks are poor candidates for blind “fast thunk” treatment.

### Longer-term compiler options

LTO/ThinLTO and PGO can improve inlining, indirect-call promotion, and code layout, but they need representative profiles and reproducible builds. Arm's PGO documentation distinguishes instrumentation and sampled workflows and requires mapping samples back to an optimized binary [11](https://learn.arm.com/learning-paths/servers-and-cloud-computing/pgo/s-pgo/).

**Implication:** PGO/BOLT is a later experiment, only if traces show front-end/locality or indirect-call costs. Do not assume it is beneficial for generated code, and do not train it on a menu or synthetic loop and call the result gameplay optimization.

## 5. JIT and code-cache lessons without adding a JIT

### Verified

Dynamic translators expose several recurring engineering problems: translation-block sizing, safe flag handling, strong-memory modes, self-modifying-code invalidation, native-code lookup, and warm-up. Box64 can also compare dynarec execution with an interpreter for debugging [3](https://raw.githubusercontent.com/ptitSeb/box64/main/docs/USAGE.md). FEX documents JIT and code-cache work as part of its ARM64 x86/x86-64 usermode implementation [12](https://github.com/FEX-Emu/FEX).

### Implication

- Larger optimized regions can improve locality but make invalidation, register allocation, correctness, and compile time harder. “Bigger blocks” is not a static-recompiler setting to copy.
- Persistent shader/pipeline caches and mod metadata should be treated like JIT caches: versioned, validated, disposable, and recoverable. They must never become a compatibility requirement or silently change semantics.
- Keep symbol/map information sufficient to attribute native samples to guest functions and imports. A profile full of anonymous generated addresses is not actionable.
- For mod/runtime patching, invalidation and hook-chain lifetime are correctness issues. Do not unload native code while a detour or callback can still execute it; the current process-lifetime plugin policy is appropriate.

## 6. ARM64 packaging and memory

### 16 KB page sizes

**Verified:** Android's 16 KB guidance and NDK build-system documentation require compatible native ELF alignment and warn against fixed 4 KB host-page assumptions [13](https://developer.android.com/guide/practices/page-sizes) [14](https://android.googlesource.com/platform/ndk/+/master/docs/BuildSystemMaintainers.md).

**Implication:**

- Verify every shipped native dependency, not only `libmain.so`: DXVK, SDL/FAudio/FFmpeg, plugin examples, hooks, driver-related libraries, and future mods.
- Distinguish ELF `PT_LOAD` compatibility, host runtime page size, and guest allocation granularity. They are related but not interchangeable.
- Use actual host page queries for host mappings. Do not change the engine's virtual layout or guest page semantics merely to satisfy host packaging.
- Keep 16 KB checks in exact-artifact CI, but do not call that proof of gameplay on a 16 KB device.

### Virtual, resident, heap, and graphics memory

**Verified:** Perfetto distinguishes VMAs/virtual memory, RSS, PSS, native allocations, ZRAM/swap, and high-water marks. Native heap profiling tracks allocator activity but can miss direct `mmap` users such as custom allocators and graphics components [15](https://perfetto.dev/docs/case-studies/memory) [16](https://perfetto.dev/docs/data-sources/native-heap-profiler).

**Implication:**

- A large reserved guest arena is not equivalent to physical memory consumed. Report virtual reservation, touched/resident memory, private dirty/PSS, swap, and high-water separately.
- Cumulative texture creation is not a live texture inventory. A memory investigation needs live size/format/mip counts and GPU/driver allocation attribution.
- Keep lazy per-mip DDS reads and bounded imports. Avoid reintroducing retained whole-file texture payloads or unbounded decoded caches.
- `d3d9.maxAvailableMemory` changes a value reported to the game; it is not a physical VRAM measurement or hard memory cap. DXVK's `d3d9.textureMemory` documentation likewise says its locked-texture virtual-memory limit does not change actual consumption or quality [17](https://raw.githubusercontent.com/doitsujin/dxvk/v2.5.3/dxvk.conf).

## 7. D3D9, DXVK 2.5.3, Vulkan, and mobile GPUs

### 7.1 Pipelines, shaders, and caches

**Verified from the pinned DXVK 2.5.3 documentation:**

- `VK_EXT_graphics_pipeline_library` can move Vulkan shader compilation to D3D shader-load time on supporting drivers, but games that load shaders only at draw time can still stutter [10](https://raw.githubusercontent.com/doitsujin/dxvk/v2.5.3/README.md).
- The older state cache remains useful mainly without graphics pipeline library. It can recompile known state on later runs but cannot predict unseen variants [10](https://raw.githubusercontent.com/doitsujin/dxvk/v2.5.3/README.md).
- D3D9 fixed-function state can generate shader variants; the HUD exposes `ffshaders`, `compiler`, `pipelines`, `drawcalls`, and memory counters [10](https://raw.githubusercontent.com/doitsujin/dxvk/v2.5.3/README.md).
- `dxvk.numCompilerThreads = 0` means all available cores; a positive value limits workers. Async compilation moves work; it does not eliminate its CPU/power cost [17](https://raw.githubusercontent.com/doitsujin/dxvk/v2.5.3/dxvk.conf).

**Implication:**

- Benchmark cold-cache and warm-cache runs separately. Record compiler activity and first-use stalls instead of averaging them into one FPS number.
- Do not disable asynchronous compilation globally to fix startup CPU usage; that can move the cost to draw time. Compare the existing bounded worker policy against alternatives on representative devices.
- Do not ship or invent a universal cache seed. Preserve cache validation, atomic backup, and recovery.
- A cache can reduce repeated compilation but cannot fix a shader/visual correctness bug by itself.

### 7.2 Capability probing and driver identity

**Verified:** Vulkan requires querying and enabling features at logical-device creation. Requesting an unsupported feature returns `VK_ERROR_FEATURE_NOT_PRESENT`; extension availability does not imply every feature in that extension is supported [18](https://docs.vulkan.org/spec/latest/chapters/features.html) [19](https://docs.vulkan.org/guide/latest/enabling_extensions.html).

**Implication:**

- Probe API version, extensions, feature structs, limits, format features, memory types, queue families, and surface/present support independently.
- Log the exact missing required feature/extension/format on failure. `VK_ERROR_FEATURE_NOT_PRESENT` alone does not identify it.
- Never infer capability from `Build.MODEL`, “Mali,” “Adreno,” a Vulkan version string, or a user-selected driver label.
- Never spoof an extension, feature, GPU vendor, or format to pass a probe. Compatibility lowering/wrappers must still expose and pass the real effective requirements.
- Keep direct system Vulkan, Turnip, PanVK, and custom-driver outcomes separate. One driver's result is not a GPU-family result.

### 7.3 System, Turnip, and PanVK driver stacks

**Verified:** Turnip is Mesa's Vulkan driver for supported Adreno hardware; the exact device and feature lists are version-specific. Mesa documents Adreno's GMEM/system-memory rendering paths and unified-memory architecture [34](https://docs.mesa3d.org/drivers/freedreno.html). The Panfrost stack includes PanVK for Bifrost and later, but its support table has generation gaps and experimental entries; Mesa warns that experimental support may be broken, require newer kernels, or be disabled by default [35](https://docs.mesa3d.org/drivers/panfrost.html). libadrenotools provides per-application rootless Adreno driver modification/replacement; it is not a system-wide driver installer [36](https://github.com/bylaws/libadrenotools).

**Implication:**

- Turnip is not a universal Adreno performance upgrade, and PanVK is not universal Mali support. Record exact source commit/package hash, GPU generation, kernel interface, and probed features with every result.
- Do not transfer an upstream Panfrost/PanVK support-table entry to an Android PanVK-kbase package with a different kernel interface and patch set. Conversely, a kbase-package failure does not disprove upstream PanVK on a supported Panthor system.
- Keep the existing private staging, signature/hash checks, isolated feature probe, and System recovery. A successful probe remains only an initialization result.
- Driver knobs such as GMEM/system-memory/binning paths are diagnostic experiments, not safe user defaults. An experimental-driver opt-in or a broken-driver escape variable is not evidence of conformance or game compatibility.

### 7.4 Mali and Adreno tiling/bandwidth

**Verified:** Arm describes tile memory being initialized at render-pass start and required outputs written back at pass end. It recommends intentional load/store operations, transient/lazily allocated attachments, and avoiding unnecessary memory round trips [20](https://support.arm.com/documentation/101897/0304/Fragment-shading/Efficient-render-passes-with-Vulkan). Qualcomm documents Adreno binning/GMEM, FlexRender, render-pass/subpass behavior, resolves, texture/compression, and driver-specific best practices [21](https://docs.qualcomm.com/bundle/publicresource/topics/80-78185-2/mobile_best_practices.html).

Shared mobile-GPU lessons:

- Bandwidth and power matter, not only shader instruction count.
- Avoid loading attachment contents that are not needed and storing attachments whose results are dead.
- Transient attachments should not incur permanent backing store when the API/driver can avoid it.
- Mid-pass clears, unnecessary resolves, readbacks, and render-pass breaks can be expensive on tilers.
- MSAA increases tile/memory/resolve pressure; a higher sample count is not a free quality setting.
- Fragment shader register pressure, texture fetch patterns, depth writes/discards, and overdraw can matter differently by GPU/driver.

**Implication:** these are capture-guided investigation rules, not permission to rewrite DXVK render passes. First establish GPU-bound behavior and identify the exact pass/resolve/barrier in an AGI/vendor-supported capture. Preserve the current AA/depth workarounds until visual and trace evidence supports a scoped change. The existing AA corruption remains unresolved.

### 7.5 Texture compression and mod packs

**Verified:** Microsoft defines BC1/DXT1 as 8 bytes per 4×4 block and BC2/BC3 (DXT3/DXT5) as 16 bytes per block [22](https://learn.microsoft.com/en-us/windows/win32/direct3d11/texture-block-compression-in-direct3d-11). Thus BC1 averages 0.5 byte/pixel and BC2/3 average 1 byte/pixel before mip/alignment overhead; RGBA8 is 4 bytes/pixel. Mesa 25.1 notes PanVK exposes `textureCompressionBC` when supported, which is explicitly conditional rather than a blanket Mali capability [23](https://docs.mesa3d.org/relnotes/25.1.0.html).

**Implication:**

- Preserving supported BC data can substantially reduce storage, upload, and sampling bandwidth versus full RGBA8 decode. Actual savings require live allocation measurements.
- Query both `textureCompressionBC` and required per-format features. Do not assume all Mali, all PanVK, or all Adreno paths are identical.
- Software decode/transcode can add startup CPU, memory, and quality loss. A 16-bit decoded path is a bandwidth/quality trade-off, not universally “optimized.”
- Keep High's full-color policy and device-specific BC behavior. Do not silently narrow textures to hide a driver defect.
- Texture replacement must retain its dimension/format/mip contract. Supporting larger or different-format mods requires engine/resource research and cannot be inferred from DDS parsing success.

### 7.6 Presentation and frame pacing

**Verified:** `VK_PRESENT_MODE_FIFO_KHR` is the required Vulkan present mode. It queues presentation requests and updates at vertical blanking without tearing; other modes have different replacement/tearing semantics [24](https://docs.vulkan.org/refpages/latest/refpages/source/VkPresentModeKHR.html).

**Implication:**

- Keep Android FIFO/interval 1 and Reset behavior. Mailbox, immediate, relaxed FIFO, or a different swapchain depth is a separate latency/tearing/stutter experiment, not a universal upgrade.
- Unlimited host FPS does not mean unlimited displayed FPS under FIFO. A 30 FPS battery-saver target is a cap/policy, not a guarantee.
- Present count and average FPS do not measure touch-to-photon latency. Input-latency claims need input timestamps and display evidence, ideally high-speed external measurement.

## 8. CPU scheduling, thermals, and measurement

### Verified

Android recommends profiling to identify CPU/GPU bounds and supports Perfetto/Simpleperf/AGI workflows where device/tool support exists [25](https://developer.android.com/games/optimize/gameperformance) [26](https://developer.android.com/agi/start). ADPF thermal headroom is device-dependent, should not be polled excessively, and sustained-performance work should be profiled and scaled gradually [27](https://developer.android.com/games/optimize/adpf/thermal) [28](https://developer.android.com/games/optimize/adpf/best-practices-adpf).

Android's threading guidance discusses cache locality and moving suitable short work to slower CPUs only after understanding the device [29](https://developer.android.com/games/optimize). This does not justify pinning every game thread to a big core.

### Implication: use controlled A/B evidence

For each performance question, record:

- exact commit/APK and native dependency/driver identity;
- device, SoC/GPU, Android build, page size where relevant;
- scene/replay/diagnostic-tour phase and settings;
- cache state (cold/warm), driver state, and whether profiling is enabled;
- wall duration and warm-up/sustained intervals;
- frame-time distribution and missed deadlines, not only average FPS;
- per-thread running/runnable/blocked time and guest/import split;
- core/frequency/thermal data where available;
- RSS/PSS/swap/high-water and graphics allocation evidence where relevant;
- visual/correctness result and logs for every failed or skipped test.

Run controlled repetitions rather than selecting the best run. Short cold-start tests and sustained thermal tests answer different questions. Detailed diagnostics can perturb timing, so compare ordinary Play as well.

### Scheduling and ADPF

- Keep the current all-available-CPU policy and ADPF session until traces show a specific placement problem.
- Do not spawn more compiler/audio/engine workers merely because cores exist. Extra threads can contend for big cores, cache, memory bandwidth, and thermal budget.
- Do not force all work onto big cores or all background work onto little cores by CPU number. Core topology and cpuset behavior vary.
- Thermal headroom is not a temperature or FPS measurement. Use it as one signal alongside actual workload and frame data.
- ADPF hints do not guarantee a frequency, core, or sustained frame rate.

## 9. Bink, audio, and I/O

### Verified

RAD documents native Android ARM/ARM64 Bink libraries, NEON paths in its history, configurable I/O/memory callbacks, and Vulkan/OpenGL integration [30](https://www.radgametools.com/binksdk.htm) [31](https://www.radgametools.com/bnkhist.htm). Those are vendor SDK capabilities, not evidence that ProtoDroid's existing Bink 1.9 ABI can be replaced by a new proprietary library without work.

### Implication

- Preserve the working FFmpeg/Bink bridge and direct YUV-plane texture path unless a profile identifies decode, conversion, upload, I/O, or pacing as the actual bottleneck.
- Measure audio/video sync and underruns with any change. Faster decode alone is not successful playback.
- Avoid adding a whole-movie preload, extra RGB conversion, or unconditional frame-skip policy for “performance.” These change memory and behavior.
- Keep default-On `Unlock menu FPS`; menu pacing and Bink waits are behavior, not just graphics settings.
- File overlays and streaming should remain bounded and off critical threads where possible, but no thread/allocator rewrite is justified from a startup log alone.

## 10. Modding, compatibility, and security

### 10.1 Compatibility mechanisms learned from recompilations

Recompilation-based mod systems commonly provide function hooks/replacements, symbol/version metadata, overlays, texture keys, and explicit runtime APIs. N64Recomp also documents single-file patch workflows that replace selected recompiled functions without rebuilding every original function [2](https://github.com/N64Recomp/N64Recomp).

**Implication:** ProtoDroid's established contracts are the right shape:

- exact engine identity and RVA/named symbols;
- dependency ordering and cycle/missing-dependency rejection;
- chained function detours rather than assuming a central dispatcher;
- append-only versioned native ABI structures;
- per-mod settings and clear enable/next-Play lifetime;
- read-only overlays that exclude saves and write/create/truncate operations;
- same-size/same-format keyed DDS replacements with bounded lazy reads.

Do not promise unchanged Windows ASI compatibility, arbitrary mod-load order, arbitrary-resolution textures, or every engine hook. Each requires separate evidence.

### 10.2 Archive safety

**Verified:** OWASP identifies archive directory traversal and symlink attacks as separate extraction risks [32](https://wstg.owasp.org/latest/4-Web_Application_Security_Testing/10-Business_Logic/09-Upload_of_Malicious_Files/).

**Implication:** retain and test all of the following:

- reject absolute paths, parent traversal, ambiguous separators/drive forms, escaping canonical paths, and case-insensitive collisions;
- reject or strictly control symlinks and pre-existing link traversal, not only `..` strings;
- enforce actual streamed per-entry/total byte, entry, mapping, depth, and time budgets rather than trusting declared ZIP sizes;
- validate nested formats after extraction with overflow-safe dimensions/row/mip calculations;
- extract into private staging and atomically promote only after complete validation;
- never silently overwrite an existing mod ID or delete outside the confirmed direct child;
- keep original game files read-only.

A data-only mod is not code execution, but its archive, images, DDS, Lua text, and metadata are still hostile parser inputs.

### 10.3 Lua and native-plugin trust

**Verified:** Lua count hooks run after a specified number of VM instructions [33](https://www.lua.org/pil/23.2.html). They are useful budgets, but they do not turn arbitrary native calls or an engine-memory API into a secure boundary.

**Implication:**

- Restricted libraries, text-only chunks, allocator limits, instruction budgets, protected callbacks, and per-state failure isolation are defense in depth, not a guarantee against malicious code.
- A Lua hook cannot interrupt a blocking or non-yielding native C call while that call is still executing. Every exposed engine/file/API function needs its own bounds and reentrancy policy.
- Engine memory writes, arbitrary guest calls, and detours make scripts trusted content even when their standard libraries are restricted. Keep the explicit trust warning.
- Native ARM64 plugins execute with the app's process privileges. An in-process ABI version check is compatibility validation, not sandboxing.
- Continue validating Android/Bionic AArch64 ELF and dependencies, staging into app-private storage, and refusing to `dlopen` shared-storage code. Do not unload process-lifetime plugins while code/hooks remain reachable.
- If truly untrusted executable mods are ever required, process isolation with a narrow IPC/API and OS resource limits is the appropriate research direction. That is a new architecture, not a small mod-loader patch.

## 11. Prioritized investigation queue

These are investigation steps, not approval to make speculative engine changes.

### P0 — establish actionable evidence

1. **Import profile baseline:** run existing detailed/import profiling during a repeatable gameplay segment. Determine guest/import/present/I/O/wait split before any thunk change.
2. **Structured capability ledger:** on each failed driver, record the exact required feature, extension, format, limit, and memory type that failed. Keep probe and later-runtime failures distinct.
3. **Frame/CPU/GPU baseline:** capture representative Mali and Adreno runs with frame times, thread states, frequencies, and supported GPU timestamps/captures. Classify CPU-bound, GPU-bound, synchronization-bound, or inconclusive.
4. **Live memory inventory:** separate guest reservation, process RSS/PSS/swap, native heap, live textures by format/mips, and driver allocations. Do not infer leaks from cumulative creation logs.
5. **Cold/warm shader runs:** record first-use stalls and compiler activity independently from warmed steady-state play.

### P1 — small candidates only after P0 evidence

6. **Hot import specialization:** target a measured, fully specified helper; add ABI/differential tests and inspect emitted AArch64. Reject if real gameplay does not improve.
7. **Compiler-worker A/B:** compare the existing bounded DXVK worker policy with alternatives for cold start, frame stalls, CPU time, and thermals. Do not judge only startup duration.
8. **BC/live-memory A/B:** on a driver with queried support, compare native BC against the existing decode path for visual equality, memory, upload/decode time, and sustained frame data.
9. **Render-pass investigation:** only for a proven GPU-bound capture, inspect exact load/store/resolve/barrier behavior. Keep any patch scoped and validate both Mali and Adreno independently.
10. **SIMD helper audit:** optimize only a measured helper, using scalar/oracle vectors and emitted-code review.

### P2 — later, higher-cost research

11. **Representative PGO/LTO/code layout:** consider only with actionable profiles, reproducible training, and enough CI/build budget to compare real artifacts.
12. **Optional ISA dispatch:** add only for a measured kernel and with baseline Armv8.0 fallback, runtime detection, and differential tests.
13. **Untrusted executable-mod isolation:** research only if that product requirement appears; do not pretend the current in-process plugin ABI is a sandbox.

## 12. Explicit non-actions and disproved shortcuts

- Do not replace static recompilation with an emulator/JIT or rewrite the engine without evidence.
- Do not globally disable x86-like ordering, NaN/rounding correctness, safe flags, or synchronization.
- Do not use `-march=native` for a distributed APK or emit optional ARM features without detection/fallback.
- Do not copy Box64/FEX preset values, Mali compiler policy, Adreno workarounds, or desktop DXVK advice as universal settings.
- Do not infer features from GPU names or relabel Vulkan capabilities to pass probes.
- Do not disable async shader compilation, change FIFO/Reset, lower texture quality, alter MSAA/FXAA, or change thread affinity as a blind performance fix.
- Do not claim that PGO, LTO, BOLT, NEON wrappers, bigger blocks, or direct thunks are faster before representative measurement.
- Do not treat virtual reservations, cache files, VRAM reporting, cumulative allocations, or file size as resident memory.
- Do not load Windows x86 ASI/DLL mods, execute native code from shared storage, or describe restricted Lua/native plugins as safe for hostile authors.
- Do not claim stable 60 FPS, zero latency, complete mod compatibility, all-GPU support, or physical-device validation from source/host/CI evidence.

## 13. 0.15 implementation disposition

This record keeps the native game/runtime architecture and the original native launcher. Later entries add only individually measured runtime or packaging corrections; none is a claim that open device issues are fixed.

| Candidate | Source disposition | Evidence boundary |
| --- | --- | --- |
| Longer automatic gameplay tour | **Implemented:** 41 phases, 39 executable plus two explicit skips, 10,440 logical action frames and 509 actions. Adds charged-jump, glide/dash, building/wall-climb attempts, sustained combat/streaming, stress and release phases. | Inputs and continuing gameplay-task activity can be logged. Rooftop arrival, wall grabs, ability results, visual correctness and audio results remain unverified. |
| Cheat-like accelerated traversal | **Implemented:** only the isolated tour scales QPC, GetTickCount, timeGetTime and RADTimerRead elapsed time by a saturating 4×. QPC frequency remains 10 MHz; wall-clock APIs, host scheduling, watchdogs and metrics stay at 1×. An atomic mode reset at every native entry keeps later normal Play at 1× without timer-path environment lookups. | Static timer routes exist, as documented above. Actual player speed, mechanic success, physics stability and Bink sync are device-unknown. No player-structure write, teleport hook or unverified engine memory patch was added. |
| Longer-run durability | **Implemented:** a 20-minute failure deadline, exact action/frame counts, planned-versus-actual totals, isolated saves/logs/cache, intentional isolated-process exit, and held-input cleanup at phase/run ends. | The deadline is a failure bound, not an expected duration or a promise that every device finishes. |
| More useful tour evidence | **Implemented:** automatic guest/import profiling, exact per-phase Present average/max with phase-boundary resets, independent rolling FPS/P99 windows, wall/process-CPU time and gameplay dispatches. The tour profiling override is set before journaling and cleared for ordinary settings on a later native entry. | CPU/Present-wall/import figures are not GPU-time measurements. Script completion is not visual verification. |
| Ordinary file-I/O overhead | **Implemented:** when detailed diagnostics are off, ReadFile/WriteFile skip diagnostic clocks, counter atomics and slow-event bookkeeping; the syscall and error behavior remain. Detailed mode retains the counters/events. | Source/host checks pass. No device I/O or FPS improvement is claimed without a before/after run. |
| ADPF overhead without a session | **Implemented:** a session-active atomic avoids the per-Present mutex when ADPF is unavailable. The mutex remains for a live session and concurrent worker registration. | This removes a specific host-lock path; it does not establish a measurable device FPS gain. |
| Auto recovery after failed Turnip | **Implemented:** on a direct <1.2 path, Auto re-runs the established direct/wrapper capability policy rather than retrying the same failed Turnip build. Explicit Turnip/Custom retry behavior is unchanged. | Affected Adreno devices must test the actual fallback. No capability is forced or invented. |
| DXVK feature failure diagnostics | **Corrected after device evidence:** the first common preflight wrongly rejected a Mali-G720 before creation. The original Vulkan 1.2 check is restored; Vulkan 1.3 again reaches `vkCreateDevice`, and exact requested-feature names are audited only after an actual `VK_ERROR_FEATURE_NOT_PRESENT`. | Supplied 0.15 normal/tour logs confirmed the regression and exact commit. The correction is source-tested; physical Mali retesting and #31/#32 outcomes remain pending. |
| BC capability detection | **Corrected after #34 evidence:** the Xclipse 940 DXVK log reports `textureCompressionBC: 0`, while ProtoDroid accepted DXT5 through `CheckDeviceFormat` and disabled decoding. DXVK's query checked format properties but not the required physical-device feature. The patched query now rejects DXT1–5 and ATI1/2 without that feature, allowing the existing native DXT decode fallback. | Fresh-clone patch and source-policy tests tie the query to the fallback; real ARM64/DXVK compilation requires exact CI. No physical Xclipse retest, complete texture-resolution result, or FPS conclusion is available yet. AA-related transparency is a separate issue. |
| UI and controls | **Corrected:** the original pre-WebView/pre-Compose native host and canvas launcher are restored, with additive native Diagnostics. Rejected WebView and Compose presentations, Gradle presentation dependencies, preview hosts, and Smart Controls remain removed. Existing driver/mod/layout tools, Classic controls, and Settings → Controls → Layout are retained. | Corrected commit `29654c7d0f904c158f424c9ea4ff13a4022d4da7` passed signed ARM64 CI. Gameplay was subsequently observed on device, but no controlled FPS comparison or broad UI/device validation was supplied. |
| OLEAUT32 ordinal dispatch and misc-import boundaries | **Corrected:** all nine ordinal imports found in the shipped engine now select their existing named handlers, using the verified export mapping rather than new stubs or changed COM semantics. `SHGetFolderPathA` and `WSAStartup` reject null output pointers with API errors before raw pointer access. | UBSan production dispatch tests cover both names and ordinals, return values, output clearing, null rejection, and stdcall cleanup; GCC's analyzer rechecks the two corrected raw-pointer paths. Static OLEAUT32 call sites exist, but no current device issue is claimed fixed without a matching runtime failure and retest. |
| Remaining WS2_32/DSOUND imports | **Corrected:** the last 16 socket ordinals now have their official named/ordinal routes and explicit offline failures; `__WSAFDIsSet` implements bounded membership. `DSOUND!#2` performs an empty DirectSound enumeration with null-callback validation. The ledger is now 364 ordinary registrations plus four special paths, with zero unresolved shipped-engine keys. | Production UBSan/fortified dispatch tests cover every added named/ordinal pair, return values, stdcall arity, `fd_set` boundaries, and empty enumeration. No online service, DirectSound renderer, or device-issue fix is claimed. |
| Native dispatch/jump-state failure paths | **Corrected:** dispatch-hash and per-thread jump-slot allocation failures now stop with detailed diagnostics instead of a null dereference. Resuming an unknown jump buffer uses the established detailed fatal route. | A UBSan setjmp/longjmp round trip verifies saved registers, FPU state and return values; a subprocess death test distinguishes the unknown-buffer fatal abort from a segmentation fault. Allocation-failure branches are source/analyzer checked, not artificially imposed on Android. |
| BC1 texture decode | **Measured candidate:** direct DXT1 output rows avoid a temporary full-colour tile. The 16-bit path also packs endpoints exactly and skips redundant alpha/interpolation work while preserving the established 8-bit mixed-colour rounding. DXT2–5 behavior is unchanged. | Two repeated host A/B runs measured about 36% less DXT1→BGRA decode time and 15% less DXT1→A1R5G5B5 time. Randomized/exhaustive exact-output UBSan tests pass. These are decoder measurements, not device FPS results. |
| DirectInput keyboard polling | **Measured candidate:** the fixed 104-key SDL map now has compile-time scancode indices and duplicate-DIK chains. A keyboard poll fetches SDL state once instead of once per key; NumLock/Pause, injected state, extended keys and object formats retain their behavior. | Two host mapping-only runs measured 95.90% and 96.49% less time than the old linear scan. The complete production map passed pinned-SDL differential checks. This is not a whole-frame or device FPS measurement. |
| Animated Canvas and controls work | **Implemented:** steady launcher/driver/mod drawing reuses geometry, font metrics and unchanged shaders; action lists use primitives. The keyboard and settings arrows reuse their draw objects. Layout-only editing no longer starts gameplay polling, and detachment cancels both repeating callbacks. | The selected steady-draw source methods have zero temporary rectangle/font-metric sites after the audit. These are allocation-site counts, not runtime allocation rates or measured FPS. Android compilation and visual/input checks remain CI/device work. |
| Legacy driver migration | **Implemented:** initialized Auto/System/Turnip launches do not inspect or copy legacy Custom packages on the UI thread. Custom and genuinely pre-preference installations retain the migration. | Host policy cases and launch-wiring checks cover the decision. No claim is made about driver runtime performance or compatibility. |

### BC1 evidence and reproducible host measurement

The supplied detailed Mali logs make CPU BC decoding a measured workload rather than a guessed hotspot. `proto-native-2.log` records 53,561 DXT decodes totaling 2,475.01 ms across its diagnostic windows; `proto-native-3.log` independently records 31,444 decodes totaling 1,752.93 ms. Individual windows exceed 200 ms of decoding and include creation/upload spikes. Other steady 30 FPS windows have zero DXT decodes, small Present wall time, and no import-profile breakdown, so they do not justify a BC1, Present, affinity, or engine-tick FPS claim.

The production benchmark is `src/native/tests/dxt_benchmark.cpp`. It uses a deterministic 512×512 corpus, seven batches, and reports median/best time and compressed-input throughput. Compare identical binaries on one host:

```sh
g++ -std=c++17 -O3 -DNDEBUG -Isrc/native \
  src/native/d3d9_dxt.cpp src/native/tests/dxt_benchmark.cpp -o /tmp/dxt-bench
/tmp/dxt-bench 400
```

On 2026-11-03, two before/after repetitions ran on the same 2-vCPU Intel Xeon 2.60 GHz x86-64 sandbox with GCC 12.2.0. Each table value is the mean of the two runs' seven-batch medians; throughput is compressed input per second, not output bandwidth.

| Decoder | Before | After | Decode-time change | Throughput change |
| --- | ---: | ---: | ---: | ---: |
| DXT1 → BGRA | 455.832 µs; 274.2 MiB/s | 291.113 µs; 429.4 MiB/s | −36.14% | +56.62% |
| DXT1 → A1R5G5B5 | 347.456 µs; 359.8 MiB/s | 295.474 µs; 423.0 MiB/s | −14.96% | +17.60% |

DXT3/DXT5 remain regression controls, not claimed optimizations. Their small timing movement between shared-host runs is not presented as a gain. Existing graphics UBSan tests compare every DXT1/2/3/4/5 output against the scalar oracle over randomized dimensions, pitches, strides, edge blocks, and guard bytes; all 65,536 endpoint values also run in both DXT1 palette modes and both output widths. Those tests pass. No ARM64 timing, visual comparison, sustained FPS, frame-latency, or thermal conclusion is inferred from this x86-64 microbenchmark.

### DirectInput and Canvas follow-up evidence

DirectInput's keyboard `GetDeviceState` previously called `SDL_GetKeyboardState` and linearly scanned all 104 mappings for each object. With almost every key up, that was the maximum scan for nearly every key on every poll. `src/native/input_lookup.h` now builds a 512-entry scancode index and 256 DIK heads at compile time. Duplicate DIKs use one-based chains, so the established NumLock/Pause entry still reports down when either physical key is down. `FindKey` uses the same immutable map. No dynamic initialization, allocation, SDL call or guest-object change was added to the lookup.

The reproducible mapping-only benchmark is:

```sh
g++ -std=c++17 -O3 -DNDEBUG -Isrc/native \
  src/native/tests/input_lookup_benchmark.cpp -o /tmp/input-lookup-bench
/tmp/input-lookup-bench 10000
```

It checks every real and injected key against the linear implementation, then alternates seven-batch medians for three repetitions. Two x86-64 host runs reported **5.890 → 0.242 µs/poll (95.90% less)** and **5.715 → 0.201 µs/poll (96.49% less)**, with matching checksums. The workload has 104 keys, one duplicate-DIK pair and almost all keys up. It isolates mapping work, excluding the guest call, output writes, lock and the rest of a frame. The separate pinned-SDL production-map differential exercised all 256 DIK values after each of the 104 physical keys was pressed and injected. CI also runs the generic UBSan test and an informational ARM64 benchmark; neither is physical-device input-latency evidence.

The Canvas audit counted source allocation sites in selected steady-draw methods, not live Java allocations. Temporary `RectF` sites went from 23 to zero in Drivers, 14 to zero in Mods, two to zero in the in-game settings panel, and 16 to zero in the previously audited Setup methods. No-argument font-metric sites went from one to zero in each animated driver/mod view and two to zero in the touch keyboard. The mod cover source rectangle and keyboard page-name array are retained, and action IDs no longer require per-frame `Integer` boxing. Pools reset before the first background draw, while persistent hitboxes still receive copies. Paragraph layouts and existing dynamic effects remain intact.

Two scratch Present-statistics variants were rejected and are not part of this batch: consumer-active synthetic runs showed overhead, and the no-consumer saving lacked evidence of a device bottleneck. Repeating FPS/screenshot callbacks were instead made attachment-owned and gameplay-only; the existing twice-per-second FPS redraw remains. These changes deliberately avoid an unsupported whole-launcher or gameplay-FPS claim.

### Open-issue disposition

| Issue(s) | 0.15 disposition |
| --- | --- |
| #22, #28 — Mali visual corruption | No visual fix is claimed. The supplied logs do not isolate a shader, depth/AA, import or timing cause; the tour can collect better evidence. |
| #23, #33 — Adreno startup/black-screen failures | Auto recovery is relevant to a failed bundled-driver attempt, but these logs were not produced by this commit. Affected-device confirmation is required. |
| #25, #29 — Vulkan 1.1 PowerVR/Mali requests | No universal Vulkan 1.1 support claim. Auto may use the wrapper only when its isolated probe verifies the effective version and required features. |
| #26 — camera/touch report | No speculative camera or touch rewrite was made; this batch does not claim to resolve it. |
| #27 — PanVK crash | Experimental Custom PanVK policy is preserved; no unverified PanVK compiler/runtime patch was added. |
| #31, #32 — DXVK feature-not-present failures | A post-failure audit names requested features without pre-rejecting the device, and Auto can try the established fallback. Neither issue is marked fixed without the affected device. |

Issue #21's UV/null-dereference constraints remain unchanged: no fabricated UV data, validation bypass or hidden null dereference was introduced.

### Validation completed before the exact-commit CI request

- All 21 Python/source wrappers passed against the fully patched DXVK fixture, including Vulkan 1.2/1.3 creation policy, signed-SDL provenance, launcher allocations, mod policies, controls and update/release guards.
- All 28 local native groups passed: UBSan/ASan runtime, memory, mods, saves, diagnostics, GPU/driver policy, state cache, input lookup, real-engine hot replacement, and production Bink at 48 kHz and 44.1 kHz. Hot replacement reported zero differences in 200 mixer, 300 biquad, 200 ADPCM and 2,000 skin trials.
- The actual production `input.cpp` and `dinput.cpp` passed strict syntax compilation with SDL 2.30.9 headers. The complete 104-key production map also passed the linear differential under UBSan.
- All 44 Android Java source/test files passed a Java syntax parser. Workflow YAML parsed, Python compilation and shell syntax passed, generated bytecode was removed, and `git diff --check` passed.
- A local JDK/Android SDK/NDK, glslangValidator and adb remain unavailable. Java host harnesses, Android compilation, DXVK shader validation, exact signed-APK verification and ARM64 CI therefore remain required; no local APK or physical-device result is claimed.

The device acceptance run must distinguish normal Play from the isolated tour and retain `result.txt`, `journal.log`, native/D3D9 logs and any crash marker. In particular, it must not describe a completed input script as proof of climbing, visual correctness, stable FPS, audio/video sync or issue resolution.

## 14. Primary-source index

1. XenonRecomp architecture/runtime boundary — [1](https://github.com/hedge-dev/XenonRecomp)
2. N64Recomp translation and metadata model — [2](https://github.com/N64Recomp/N64Recomp)
3. Box64 dynarec correctness/performance controls — [3](https://raw.githubusercontent.com/ptitSeb/box64/main/docs/USAGE.md)
4. Android ABI definitions — [4](https://developer.android.com/ndk/guides/abis)
5. Arm AAPCS64 — [5](https://github.com/ARM-software/abi-aa/blob/2bcab1e3b22d55170c563c3c7940134089176746/aapcs64/aapcs64.rst)
6. Windows ARM64 ABI/unwind comparison — [6](https://learn.microsoft.com/en-us/cpp/build/arm64-windows-abi-conventions?view=msvc-170)
7. Arm memory ordering — [7](https://documentation-service.arm.com/static/62a304f231ea212bb662321d)
8. FEX x86 memory-model analysis — [8](https://fex-emu.com/Scourge-of-emulation/)
9. Arm SSE-to-Neon porting guidance — [9](https://support.arm.com/documentation/102581/0200/Port-with-SSE2Neon-and-SIMDe)
10. DXVK 2.5.3 README — [10](https://raw.githubusercontent.com/doitsujin/dxvk/v2.5.3/README.md)
11. Arm sampled-PGO workflow — [11](https://learn.arm.com/learning-paths/servers-and-cloud-computing/pgo/s-pgo/)
12. FEX dynamic translation project — [12](https://github.com/FEX-Emu/FEX)
13. Android 16 KB guidance — [13](https://developer.android.com/guide/practices/page-sizes)
14. NDK build-system/ELF guidance — [14](https://android.googlesource.com/platform/ndk/+/master/docs/BuildSystemMaintainers.md)
15. Perfetto Android memory case study — [15](https://perfetto.dev/docs/case-studies/memory)
16. Perfetto native heap profiler — [16](https://perfetto.dev/docs/data-sources/native-heap-profiler)
17. DXVK 2.5.3 configuration — [17](https://raw.githubusercontent.com/doitsujin/dxvk/v2.5.3/dxvk.conf)
18. Vulkan feature requirements — [18](https://docs.vulkan.org/spec/latest/chapters/features.html)
19. Vulkan extension/feature probing — [19](https://docs.vulkan.org/guide/latest/enabling_extensions.html)
20. Arm efficient Vulkan render passes — [20](https://support.arm.com/documentation/101897/0304/Fragment-shading/Efficient-render-passes-with-Vulkan)
21. Qualcomm Adreno mobile best practices — [21](https://docs.qualcomm.com/bundle/publicresource/topics/80-78185-2/mobile_best_practices.html)
22. Microsoft BC formats — [22](https://learn.microsoft.com/en-us/windows/win32/direct3d11/texture-block-compression-in-direct3d-11)
23. Mesa 25.1 PanVK conditional BC support — [23](https://docs.mesa3d.org/relnotes/25.1.0.html)
24. Vulkan present modes — [24](https://docs.vulkan.org/refpages/latest/refpages/source/VkPresentModeKHR.html)
25. Android game-performance guidance — [25](https://developer.android.com/games/optimize/gameperformance)
26. Android GPU Inspector — [26](https://developer.android.com/agi/start)
27. ADPF thermal guidance — [27](https://developer.android.com/games/optimize/adpf/thermal)
28. ADPF best practices — [28](https://developer.android.com/games/optimize/adpf/best-practices-adpf)
29. Android threading/locality guidance — [29](https://developer.android.com/games/optimize)
30. Bink SDK platform integration — [30](https://www.radgametools.com/binksdk.htm)
31. Bink implementation history — [31](https://www.radgametools.com/bnkhist.htm)
32. OWASP archive traversal/symlink testing — [32](https://wstg.owasp.org/latest/4-Web_Application_Security_Testing/10-Business_Logic/09-Upload_of_Malicious_Files/)
33. Lua hook mechanics — [33](https://www.lua.org/pil/23.2.html)
34. Mesa Freedreno/Turnip architecture and support — [34](https://docs.mesa3d.org/drivers/freedreno.html)
35. Mesa Panfrost/PanVK support and experimental-status warning — [35](https://docs.mesa3d.org/drivers/panfrost.html)
36. libadrenotools per-app rootless driver loading — [36](https://github.com/bylaws/libadrenotools)
37. REA built-in PE inspection, GNU objdump Evidence, and provider boundaries — [37](https://github.com/morluto/rea/tree/2cb29693c3d574c4898832047fe309ac1ad00e9d)
