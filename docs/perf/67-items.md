# ProtoDroid performance program: the 67 items

This is the working ledger for the 67-issue performance review of ProtoDroid (Android, ARM64, Snapdragon-class
device). It records, for every item, what the problem was, what was changed, what could **not** be changed from this
side, and how the result is checked. `tools/perf_critic.py` scores this ledger and re-verifies each claim against the
tree; `tools/test_perf_program.py` is the hard gate it runs.

Two rules govern the whole document:

1. **No unmeasured claim is written as a result.** Anything whose benefit depends on the device (clock behaviour,
   FUSE throughput, driver scheduling, cache hit rates) is marked *needs a device A/B* and the measurement command is
   given.
2. **Nothing that could be measured on a host is left untested.** The guest allocator, the runtime bridge and the
   diagnostics policy have runnable tests; the critic fails if they regress.

Status words used below:

| Status | Meaning |
| --- | --- |
| **done** | Implemented in this tree; a check in `tools/test_perf_program.py` covers it. |
| **done (unmeasured)** | Implemented; the size of the win needs the device A/B named in the entry. |
| **partial** | The mechanism landed, but part of the fix needs work outside this change (DXVK, Mesa, the generated C). |
| **rejected / deferred** | Deliberately not changed here, with the reason and the alternative recorded. |

## How the rating works

`python3 tools/perf_critic.py` scores each item 0.0-1.0 from its evidence checks and takes the **severity-weighted**
mean (Critical 5, High 3, Medium 2, Low 1) scaled to 10. The test suite is a multiplier, not an addend: if
`tools/test_perf_program.py` fails the rating is multiplied by 0.85, so a tree cannot be argued into a good score;
it has to pass its own checks. `--min 8.5` makes CI fail below the target.

The scale is deliberately conservative:

| Score | Meaning |
| --- | --- |
| 1.00 | implemented, covered by a check |
| 0.85 | implemented; the size of the win is a device measurement that has not been run |
| 0.70 | not changed, with the boundary, the alternative and the validation written down |
| 0.55 | part of the mechanism landed; the rest provably lives outside this change |
| 0.00 | not addressed: no code and no justification |

`tools/test_perf_critic.py` is the guard against a rigged scorer: it copies the tree into a temporary directory, breaks
one claim at a time - deletes the path-resolution cache, puts the DXT decode back under the bridge lock, restores the
launcher's per-vsync redraw, deletes a deferred item's justification - and requires the rating to fall, and to fall
below 8.5 for the critical one. It runs in CI next to the critic itself.

---

## Critical

### 1. There is no whole-program optimisation across the 192 recompiled translation units

`src/native/Makefile` now builds with `-flto=thin` (`LTO ?= 1`), a shared `thinlto-cache-dir`, and links the recompiled
TUs as bitcode so the thin link can inline the small guest accessors (`R32`, `W32`, `Ptr<T>`) into 190k call sites.
**Status: done (unmeasured).** The size and speed effect needs a device A/B; the compile-time effect is visible in CI
logs. Verify: `make -C src/native ... V=1 | grep flto`, then compare `libmain.so` size and a boot-to-game trace.

### 2. There is no profile-guided optimisation flow

PGO is wired as a two-pass flow (`PGO_MODE=generate|use`, `-fprofile-instr-generate` / `-fprofile-instr-use`,
`-mllvm -pgo-warn-missing-function`). The profile has to come from a **device** run because the branch mix is what the
optimiser needs: generate during a diagnostic tour, pull `/data/local/tmp/*.profdata`, rebuild with `PGO_MODE=use`.
`-fprofile-partial-training` keeps the cold recompiled bodies size-optimised instead of bloating them.
**Status: done (unmeasured).** Verify: two APKs, same scene, `simpleperf stat -e cycles` per frame.

### 3. Every function is in one section, so nothing can be reordered or garbage-collected

`-ffunction-sections -fdata-sections` plus `--gc-sections` at link; hot/cold splitting and symbol ordering are
documented as profile-driven follow-ups (BOLT-style layout needs the PGO profile from item 2).
**Status: done (unmeasured).**

### 4. Roughly 127,000 recompiled guest functions are exported from `libmain.so`

Exporting them costs a dynamic symbol per function (hash table, GOT, relocation processing at load) and prevents the
linker from localising or inlining them. `src/native/libmain.map` now exports only `SDL_main` (SDL's Java glue
`dlopen`s `libmain.so` and `dlsym`s it), `Java_*` (the natives the Java side binds after that load) and
`protodroid_device_api_level` (the `--defsym` target for libadrenotools); `-fvisibility=hidden` is the default;
`--export-dynamic` is gone. Guest function names are recovered from the recompilation table instead
(`RecompNameForAddress`), so crash traces stay readable: `watchdog.cpp` and `DescribeAddress` both use it.

Two things this item had to learn from the real ARM64 build, both now asserted:

* **A version script cannot export a symbol the compiler hid.** `-fvisibility=hidden` wins, the symbol silently
  disappears from `.dynsym`, and the link still succeeds - so the first real build of this branch produced a
  `libmain.so` without `protodroid_device_api_level` and the APK verification step caught it
  (`llvm-nm -D libmain.so | grep protodroid_device_api_level`). Definitions named in the map now carry
  `PROTODROID_EXPORT` (`src/native/exports.h`); `SDL_main` does not need it because SDL's own `SDL_main.h` declares it
  with `SDLMAIN_DECLSPEC`, which is `__attribute__((visibility("default")))` on Android. `tools/test_perf_program.py`
  proves the pair by linking a stub against the real `libmain.map` with the same flags and reading `.dynsym` back, and
  rejecting a map entry whose definition lost the attribute; the APK step also checks `SDL_main` and
  `protodroid_device_api_level` in the packaged library, since a missing `SDL_main` is a launch failure, not a slow
  frame.
* **A name in the map must exist in every variant that links against it.** `protodroid_device_api_level` sits outside
  the Adreno-only `#if`, so an `x86_64` build without the Turnip/AdrenoTools libraries still links.

**Status: done (unmeasured).** Verify: `nm -D --defined-only libmain.so | wc -l` before/after (expected 127k -> < 20);
`llvm-nm -D lib/arm64-v8a/libmain.so` on a device-pulled APK should show `SDL_main`, the `Java_*` natives and
`protodroid_device_api_level`, and nothing else.

### 5. The dependencies are built for generic ARMv8

`android/build-deps.sh` now builds every dependency for the device instead of with CMake/Meson defaults:

* one `DEPS_CFLAGS` (`-O3 -mtune=cortex-a76 -fomit-frame-pointer -ffunction-sections -fdata-sections
  -fno-math-errno`) shared by SDL2 and FAudio, passed through `CMAKE_<LANG>_FLAGS_RELEASE` with
  `CMAKE_BUILD_TYPE=Release`;
* the DXVK cross file gets `buildtype = 'release'` and `b_ndebug = true` plus the same tuning flags;
* FFmpeg is configured with `--enable-neon --enable-optimizations` so the Bink audio decoders are not scalar.

`-mtune` (not `-march`) is used on purpose: the same binary must run on every supported device.

Two decisions worth recording, and being precise about what is known versus inferred:

* **The flags go into `CMAKE_<LANG>_FLAGS_RELEASE`, never into `CMAKE_<LANG>_FLAGS`.** The first version overrode
  `CMAKE_C_FLAGS` on the command line and added `CMAKE_INTERPROCEDURAL_OPTIMIZATION=ON`. That change failed the
  dependency job roughly 70 seconds into the build - a cold rebuild (the cache key hashes this script, so the
  dependency tree was rebuilt from source) - and the job log could not be retrieved from this environment (the log
  blob host is unreachable), so the cause below is inferred from the code, not proven by the log. Two things are
  certainly wrong with it: overriding `CMAKE_C_FLAGS` from the command line is how a dependency build loses the NDK
  toolchain's target triple, `-DANDROID` and `-fPIC`, and interprocedural optimisation makes each dependency run a
  compiler-support check that was never exercised before. Both are gone: release flags are appended after the base
  flags, and the failure annotation now prints the actual diagnostics on the next failure.
* **Dependency LTO is not enabled by default.** It is reachable through `LTO_DEPS=1` (DXVK's `b_lto`), but the
  dependency job is a cold full rebuild from source in CI, where a full-libLTO link buys an unmeasured gain at a real
  build-time and memory cost. `b_ndebug=true` still keeps assertions out of the shipped DXVK.

**Status: done (unmeasured).** Verify: `llvm-objdump -d build/…/libdxvk_d3d9.so | grep -c fmla` (or `readelf -A`), then a
frame-time A/B of the same scene.

### 6. FP contraction is off for every float operation, including the hot native kernels

`FAST_FP=1` enables `-ffast-math -ffp-contract=on` for the native kernels (skinning, audio mix, Bink blit). It is off
by default because the audio path must stay bit-exact, so the flag is per-file, not global.
**Status: partial.** The audio path is deliberately excluded; the skinning path needs a device A/B before it ships on
by default.

### 7. Diagnostics run in the normal play path

`DXVK_LOG_LEVEL` is `warn` unless the diagnostic tour is active (`info`); the import profiler is a single `bool` read
per call, resolved once at start-up; the watchdog only formats when armed.
**Status: done.**

### 8. There is no third CPU tier for ARMv8.4+ (unaligned atomics, RCpc2)

The tier is documented and selectable in the build, but the code generator has to know about it before it is useful.
**Status: partial** — see item 10.

### 9. TSO is emulated with a full barrier on every shared access

Each `ldar`/`stlr` in the guest costs a `dmb ish` on ARM64, and the device is still the original Snapdragon: this is the
single largest remaining CPU cost in scene-graph and job-system code.
**Status: deferred — this one cannot be fixed from `src/native`.** ProtoDroid links pre-generated C (412 MB under
`build/recomp/`), so any per-function relaxation has to be decided by `tools/recomp` and the output regenerated; a
runtime-only change cannot tell which guest accesses are actually single-threaded. The plan that keeps correctness:

* `tools/recomp` records, per function, whether it can reach a threaded primitive (CreateThread, a lock, a futex, a
  message queue). A function that cannot is *thread-confined* and does not need real acquire/release semantics.
* Thread-confined functions are emitted with plain accesses plus a single `dmb ish` at the (rare) call to a
  synchronising import — the same argument XenonRecomp uses for ABI-qualified register localisation.
* Everything else keeps the exact sequence it has today, so the failure mode of a wrong classification is *slower*,
  not *incorrect*, and the whitelist is a data file that can be reverted without touching code generation.
* Validate with a lock-stress scene (combat with the job system busy) plus the existing save/reload tour.

Until that regeneration happens, weakening the ordering in the interpreter would be unsound and is **rejected**.

### 10. Every 16/32/64-bit guest access tests for alignment before an RCpc atomic

`src/recomp/runtime.h` exposes `RECOMP_UNALIGNED_ATOMICS`; where the build targets the `armv8.4-a+rcpc` tier the
`ldapr`/`stlr` forms are used directly and the alignment branch disappears. Everything below that tier keeps the
branch, because `RCpc2` is what makes unaligned release stores correct.
**Status: partial** — the runtime hook is in place, the code generator must select it (same regeneration as item 9).

### 11. Every call spills and reloads all eight argument registers

Generated code stores `r3..r10` before every call and reloads them after, because the analysis is conservative about
which of them the callee preserves.
**Status: deferred — needs regenerated code.** The fix belongs in `tools/recomp`: an interprocedural pass that computes
which parameter registers the callee actually reads, plus a fallback for indirect calls. A hand edit of the generated C
would be undone at the next regeneration.

### 12. The x87 stack is copied in and out of every function that uses floats

Same story: `tools/recomp` knows the static x87 stack depth of each function; copying eight 80-bit slots per call is
only necessary when the depth is unknown (recursion through an indirect call).
**Status: deferred — generated code.** The generator change is a dataflow over the same instruction table it already
walks; it cannot be expressed in `src/native` without the runtime guessing which FP state is live.

### 13. The return address is pushed through the emulated guest stack

A native return saves two guest stores per call and, more importantly, stops the host return-stack predictor from being
trained on the guest stack.
**Status: deferred — generated code** (same regeneration as items 11 and 12).

### 14. Indirect dispatch hashes through a 4 MiB table on every call

`runtime.cpp` builds the hash once (`RecompInit`, called from `main.cpp` before the game starts) and puts a
monomorphic inline cache in front of the hash: a repeated call to the same target costs one compare and a tail call.
`call_once` and the lazily built table are gone from the hot path.
**Status: done.** Verify: `test_runtime_bridge_test.sh` covers the cache and the name lookup.

### 15. The import dispatcher is a heavyweight stub per import

`imports.cpp` now keeps a dense `Entry { handler, win_name, host_name }` array, so a dispatch is an index and one load
from a cache line that holds everything it needs; the per-call function-local static and the string comparison are
gone, and profiling is gated by one boolean.
**Status: done.** Verify: `run_runtime_bridge_test.sh` (it reads the RPCRT4 path) and the import-count report.

### 16. CRT shims are slower than the libc they wrap

`memcpy` is routed to `memmove` only where overlap is possible; the general shim uses the host `memcpy`. The allocator
side of this item is covered by items 28-30.
**Status: done.**

### 17. Every event, semaphore and wait shares one global mutex

Kernel objects carry their own atomic `WaitWord`; signal is a `FUTEX_WAKE_PRIVATE` and wait is a `FUTEX_WAIT_PRIVATE`
with a compare-and-swap fast path, so an uncontended event costs one atomic and no lock. `legacy_waiters` keeps the old
path alive only for the handful of objects created before the change.
**Status: done.** Verify: `test_*` kernel-object tests plus the thread-stress tour.

### 18. `WaitForMultipleObjects` allocates on every call and wakes the whole herd

The first eight handles live in a stack array (`kInline = 8`); auto-reset objects wake exactly one waiter
(`e->manual ? INT32_MAX : 1`), manual-reset objects still wake all.
**Status: done.**

### 19. `CRITICAL_SECTION` is a hash map lookup into a `std::recursive_mutex`

Locks live in the guest structure itself: a CAS on the lock word, a short spin (`kCsSpin`) for the uncontended
contended case, then a futex wait keyed by the same word, so a lock/unlock pair in a tight loop no longer touches a
host container at all.
**Status: done.**

### 20. The handle table is O(n) on create and races on the refcount

Index free list (`g_handle_free`) for O(1) create, atomic `refs`, and deferred deletion so a handle closed while a
waiter holds it is freed by the last user (`ReapDeferredLocked`).
**Status: done.**

### 21. Thread creation: 1 MiB stacks, a yield handshake, unnamed threads

Guest threads now get 8 MiB stacks (`pthread_attr_setstacksize`) — the guest assumes 1 MiB of guard space and was
crashing into the host stack on the job system — a condition-variable handshake instead of `sched_yield` spinning, and
a name (`proto-guest-NN`) so `simpleperf`, Perfetto and ADPF can identify them.
**Status: done.**

### 22. Thread stacks, TEBs and fiber stacks are never freed

`DetachThread` releases the stack and the TEB; fiber stacks are pooled per size and released (`ReleaseFiber`) once the
pool is full, instead of growing without bound over a long session with mod scripts.
**Status: done.**

### 23. `FiberSwitch` takes a global mutex twice

Fiber slots are recycled through a free list (`g_fiber_indices`) and the current fiber is cached on the thread, so a
switch is a table lookup and the assembly, not two lock acquisitions.
**Status: done.**

### 24. `Sleep()` causes wake-up storms and spin loops

`Sleep(0)` backs off after 64 consecutive yields instead of spinning the core; sub-2 ms sleeps go to `usleep` rather
than a timer; each guest thread sets a 500 us `PR_SET_TIMERSLACK` so the kernel coalesces wake-ups.
**Status: done (unmeasured).**

### 25. Thread priority is only a nice value and ADPF misses most threads

No log line per priority change; background threads are placed on `SCHED_BATCH`; `PerfHintAddNamedThreads` completes
the ADPF session from `/proc` once the engine has started its threads, adding the DXVK, guest and audio threads that
were previously invisible to the power model.
**Status: done (unmeasured).** Verify: `adb shell dumpsys power | grep -i adpf` shows the session thread count.

### 26. No big-core steering below Android 12

`SetThreadAffinityMask` now applies the guest's mask with a real `sched_setaffinity` instead of being ignored, so the
engine's own main-thread pinning works on Android 11 and below.
**Status: done.**

### 27. The watchdog can report stale tids, recycled threads, and it logs from a signal handler

The handler only captures lock-free state (`OnSignal`); formatting and logging happen on the watchdog thread
(`ReportTrace`), slots are released when a guest thread exits (`UnregisterGuestThread`), the whole thing is pausable
(`SetWatchdogPaused`), and PCs are named from the recompilation table.
**Status: done.**

### 28. A global heap mutex on every guest allocation

Per-thread magazines per size class (`thread_local Magazines`, `kMagazineBlocks` bounded): allocation and free of a
small block are a pointer bump in a thread-local magazine; the global lock is taken only to refill or return a batch,
and the magazine is flushed on thread exit (`Magazines::~Magazines`).
**Status: done.** Verify: `run_memory_test.sh` (allocation/free/return-to-OS semantics) plus the allocation-rate scene.

### 29. Large allocations: `mmap` per call, first-fit map, 64 KiB rounding

Heap blocks now commit at the 4 KiB host page (`kHostPage`, `PageAllocLocked(total, 0, kHostPage)`), so a 100 KB
allocation costs 25 pages instead of two 64 KiB units and the first-touch fault storm shrinks with it.
**Status: done.**

### 30. Size classes: linear lookup, abandoned tails, nothing returned

`g_class_lookup` makes the class lookup O(1) and chunk tails are recovered (`hand its tail`) rather than wasted.
**Status: done.**

### 31. The D3D9 wrapper table grows forever

`HostTable` recycles indices through a free list; `Forget` returns the slot (`g_hosts.Release`).
**Status: done.**

### 32. First-touch page-fault storms

The commit granularity in item 29 is the part that can be fixed here; explicit prefaulting is a device A/B (it trades
faults for up-front work, which is a loss on a memory-constrained device).
**Status: partial.**

### 33. DXT textures are decoded while holding the global D3D9 mutex

Map surgery happens under the lock and the entry is *moved out* (`entry = std::move(it->second)`); the decode, the mod
replacement and the upload then run unlocked. A second thread can no longer stall a presenter behind a 4 MB decode.
**Status: done.**

### 34. One recursive mutex and node maps for all bridge state

Lock/unlock only touches the adapted-lock map to move the entry out; the work happens outside it.
**Status: done.**

### 35. `WrapAs` builds a `std::string` per call

A dense literal-pointer table (`g_vtable_slots`, `VtableFor`) replaces the string-keyed map: one probe per lookup, no
allocation, no tree walk.
**Status: done.**

### 36. Lock shadows copy the whole resource in and out

The lock plan is consulted so the discard/no-overwrite cases skip the copy-in; the persistent-shadow work needs a
device A/B because it changes when the guest sees its own data.
**Status: partial.**

### 37. DXT textures are decoded to RGBA on GPUs that support block compression

Turnip on this device does not expose `BC*` for D3D9 formats, so every texture is decoded on the CPU at load time and
stays 4x larger in memory. The fix is a block transcoder (BC1 -> ETC2 RGBA / BC3 -> ASTC 4x4) plus an on-disk cache
keyed by content hash so a texture is transcoded once per install.
**Status: deferred — outside this change.** It needs a transcoder that does not exist in this tree and a cache format
that survives an app update; both are a project of their own, and a wrong transcode is a visible visual defect, so it is
not being landed unmeasured. Items 33 and 38 remove the *lock* cost and the *scalar* cost of the current path, which is
what makes it tolerable until then.

### 38. The DXT palette expansion is scalar

The decoder already stores tiles through NEON (`StoreFullTile`); expanding the 4-colour palette for 16 texels at once
needs the vector rewrite, which is measured with item 33's scene.
**Status: partial.**

### 39. Mip skipping happens after the data has been read

Skipping a mip level still reads it from the archive and then discards it. The hook has to be in the loader (before the
read), which is behind the same regeneration boundary as items 11-13.
**Status: deferred.**

### 40. Mod texture matching hashes every texture under two locks

Three changes:

* the digest is computed outside the bridge lock (item 33);
* it is computed at most once per texture object (`identified`), which is where it belongs;
* **most textures are no longer hashed at all.** A replacement can only match when its DDS has the original's format and
  base dimensions - that is enforced when a DDS is loaded - so `g_candidate_shapes` holds every candidate shape, read
  from the 128-byte DDS headers at scan time. A texture whose shape has no candidate returns before the SHA-256 pass.
  If any header cannot be read the shortcut disables itself, because then "no candidate" is no longer provable.

**Status: partial.** The remaining half is the SHA-2 hardware extension. The instruction sequence is written and then
**deliberately not enabled**: the vector-lane layout of `vsha256hq_u32`'s operands cannot be validated in this
environment (no ARM toolchain, no device), and a wrong digest does not fail loudly - it silently stops matching mod
textures. A self-test-and-fallback version was prototyped and rejected as unverifiable; the shape index removes the
cost for the common case without that risk.

### 41. No shader or pipeline pre-warm, and Turnip's shader cache is discarded

`MESA_SHADER_CACHE_DIR` and `MESA_SHADER_CACHE_MAX_SIZE` are set before the driver is loaded, so compiled shaders are
written to app storage instead of `/data/local/tmp` or nowhere, and the second launch of a scene reuses them.
**Status: partial.** The cache is the half that lives in this tree; pipeline pre-warm requires DXVK changes (compile the
pipelines the engine uses during the loading screen instead of at first draw).

### 42. The whole state cache and log are re-copied every 30 seconds

Change detection on size+mtime (`FileChanged`, `SameFileContents`), 256 KiB atomic copy buffers, and a 5 minute cadence
(`seconds(300)`) instead of 30 s — over FUSE, the previous behaviour rewrote several MB every half minute for nothing.
**Status: done.**

### 43. The present mutex is held across the vsync wait

The surface flag is atomic and presenters are counted (`g_presents_in_flight`); the UI thread waits at most 150 ms for
an in-flight present (`g_present_idle.wait_for`) instead of blocking behind it, and the mutex is gone from this path.
**Status: done.**

### 44. ADPF is fed the present-to-present interval

The hint now receives this thread's CPU time per frame (`CLOCK_THREAD_CPUTIME_ID` -> `PerfHintFrame(work_ns)`), which is
what the API expects; feeding the wall interval made the power model believe the app was permanently over budget and
throttle it.
**Status: done (unmeasured).**

### 45. The ADPF session misses the DXVK and worker threads

`PerfHintAddNamedThreads` scans `/proc/self/task/*/comm` once the engine has started its threads and adds the DXVK,
guest and audio threads to the session (item 54 keeps shader-compile and asset-load work out of it).
**Status: done (unmeasured).**

### 46. The display is never told the frame rate

`ANativeWindow_setFrameRate(FIXED_SOURCE)` is called with the configured cap (`ApplyDisplayFrameRate`) so the panel and
the framework can pick a matching mode instead of defaulting to 60 Hz for a 90 Hz render.
**Status: partial.** Present-paced limiting (do not queue a frame earlier than the cap allows) is the other half.

### 47. No thermal-aware adaptation

**Status: deferred.** A thermal controller needs the ADPF thermal headroom to be sampled and a policy with hysteresis
(cap -> render scale -> effect level) that a reviewer can argue about; shipping a guessed policy is how devices end up
with unpredictable quality. The inputs it needs (items 44, 45, 46) are in place now.

### 48. No Game Mode or sustained-performance integration

`ProtoActivity` sets `setSustainedPerformanceMode(true)` so the platform stops quietly dropping clocks mid-session, and
reports the game state so the power HAL can apply its game-loading boost and its per-game profile:

* `onCreate` -> `setGameState(new GameState(true, GameState.MODE_NONE))` (loading),
* `onResume` after the first -> `(false, GameState.MODE_GAMEPLAY_UNINTERRUPTIBLE)` (in gameplay),
* `onPause` -> `(false, GameState.MODE_NONE)` (interruptible).

Getting this right took three attempts, all of them rejected by CI's javac:

1. `setGameState(int)` on API 31 - the method arrived in **API 33** and takes an `android.app.GameState` object;
2. `new GameState(..)` with `GAME_STATE_*` names - those belong to Google's Adaptability codelab enum, not to the
   platform class, whose modes are `MODE_UNKNOWN`, `MODE_NONE`, `MODE_GAMEPLAY_INTERRUPTIBLE`,
   `MODE_GAMEPLAY_UNINTERRUPTIBLE` and `MODE_CONTENT` (confirmed against the API reference);
3. a missing `Build` import for the API-level guard.

The javac step now emits its diagnostics as check annotations, `tools/test_java_sources.py` reports static access on a
type that is not imported, and the gate asserts the `MODE_*` names, so the same mistake fails locally next time.
**Status: done.**

### 49. Game data is read through FUSE, with no install-to-internal option

**Status: deferred.** Moving the imported game to app-internal storage removes the FUSE round trip from every read
(items 50 and 51 soften it for now), but it needs a launcher import flow with progress, free-space checks, resumable
copy and a rollback path. Recorded as the largest single I/O win available.

### 50. `HostPath` stats every component and scans directories on open

Case resolution is cached per `(directory, name)` behind a shared mutex (`g_case_cache`), and a miss for a writable
folder (saves, logs) is not cached so newly created files are still found. Read-only asset directories resolve to a table
lookup instead of one `stat` plus, on a miss, a full `opendir`/`readdir`/`strcasecmp` scan.
**Status: done (unmeasured).**

### 51. `ReadFile` has no readahead and runs every "overlapped" I/O synchronously

Sequential detection per handle plus `posix_fadvise(WILLNEED)` 512 KiB ahead, `POSIX_FADV_SEQUENTIAL` for movies and
`POSIX_FADV_RANDOM` for the archives at open.
**Status: partial.** Real asynchronous completion (the guest's overlapped I/O completing on a worker thread) is the
remaining half; it changes guest-visible ordering, so it needs its own device A/B.

### 52. `Log()` takes a global mutex, writes logcat and two files, and flushes per line

Callers format one line into a bounded queue (`kLogQueueLines`); a single writer thread (`LogWriterLoop`) owns logcat
and the files, and the queue is drained on exit (`FlushLogQueue`). A frame no longer waits on file I/O because a
diagnostic line was emitted.
**Status: done.**

### 53. `DXVK_LOG_LEVEL=info` for everyone

`warn` by default, `info` only for the diagnostic tour.
**Status: done.**

### 54. Bink opens a second audio device

**Status: deferred.** Routing movie PCM into the FAudio mastering voice needs changes in the Bink callback and the
XAudio2 mixer; a wrong mix is an audible defect.
**Partial mitigation:** item 55 removes the per-sample blit cost, which is the part of the movie path that competes
with the mixer for CPU.

### 55. The Bink blit is a scalar per-pixel loop

Aligned, unscaled chroma planes are copied row by row (`memcpy`) instead of per sample with two clamps
(`chroma planes are contiguous rows`); the unscaled luma copy was already a `memcpy`.
**Status: done.**

### 56. The audio mixer thread takes a recursive mutex and logs inside the callback

**Status: deferred.** The fix is lock-free object lookup plus `POWER_SAVING` AAudio mode, and it needs on-device audio
validation.

### 57. The launcher animates continuously in the background

The backdrop steps at 30 fps (`postInvalidateDelayed(33)`) and stops completely while the settings panel is open; the
sweep gradient is built once into a `LinearGradient` and moved with `setLocalMatrix` instead of allocating a gradient
and an `int[]` per frame.
**Status: done.** This is the battery/heat win that protects the game's sustained clocks.

### 58. Launcher bitmaps are decoded at full size, stay resident, and `largeHeap` hides the cost

`RGB_565` decode (18.7 MB -> 9.3 MB for four 1440x810 slides), `releaseArtwork()` recycles the slides while the game is
in front and `reloadArtwork()` reloads them on return, and `android:largeHeap` is removed from the manifest.
**Status: done.**

### 59. The launcher and the game share one process

**Status: deferred.** A `:game` process would isolate the launcher's heap and let the framework reclaim it, but the
activities hand configuration over through static state today; that refactor belongs with item 49.

### 60. `onCreate` does disk work and re-copies the plugin tree on every launch

The staging directory is stable (`mod-native-current`) instead of a fresh UUID per launch, `reusableStaging` compares
the staged entry's mtime against the newest source file and skips the copy when nothing changed, and staged plugins whose
id is no longer enabled are deleted.
**Status: done.**

### 61. `check()` probes FUSE storage on the UI thread

The probes (`mkdirs`, shared-storage access, game-file scan) run on a single-thread executor and post their result back
(`checkAsync`), so the launcher cannot ANR on a slow SAF mount.
**Status: done.**

### 62. The update check runs on every resume

Automatic checks are throttled to six hours.
**Status: done.**

### 63. The touch overlay builds a string cache key per button per frame and never frees bitmaps

Each button keeps its cached bitmap with an `int` state hash (`buttonStateHash`), and the shared cache is a bounded
access-ordered LRU that recycles evicted bitmaps (the button only drops its reference).
**Status: done.**

### 64. The touch overlay polls in loops

The statistics refresh returns immediately unless the overlay is shown, and the screenshot poller only runs in a
debuggable build (`uiShotPollEnabled`). The refresh also invalidates only the statistics band (`dirtyStatsRect`) rather
than the whole overlay: the line is drawn at (16dp, 74dp) in 13sp text, and redrawing every control twice a second on
top of the running game was the actual cost.
**Status: done.**

### 65. The overlay is invalidated wholesale

Only the touched control's region is invalidated (`dirtyControlRect`) instead of the full-screen overlay.
**Status: done.**

### 66. `extractNativeLibs`, both CPU tiers and the bundled driver stack are in every APK

**Status: rejected — deliberately.** The package is 25.3 MiB today. It could be smaller: `extractNativeLibs="false"`
with unpaginated alignment, dropping the ARMv8.0 fallback engine, and shipping the Turnip images as an optional
download. Each of those trades size for a behaviour the user explicitly required to keep — the fallback engine is what
lets a device without LSE atomics boot at all, and the bundled driver stack is what makes Adreno devices render
consistently. The package also has to stay a single universal APK (no ABI split, no bundle) because the distribution
path is a direct APK download. Recorded as a packaging decision for the maintainer, not a code change.

### 67. The repository architecture blocks measurement

There is now a first-class measurement path: this ledger, `tools/perf_critic.py` (which re-verifies every claim and
fails CI below 8.5), `tools/test_perf_program.py` (the gate it runs), `tools/test_java_sources.py` (structural
verification of the Java half, since no JDK exists in the build environment) and `tools/perf_evidence.py` (unchanged,
still the only place aggregate numbers come from).

Build time is addressed too: `CCACHE=1` routes the compiler through ccache, the workflow installs ccache, caches
`~/.ccache` between runs and prints `ccache -s`, so editing `src/native` no longer recompiles the 192 untouched generated
translation units. The Makefile only applies the prefix when `command -v ccache` succeeds, because the first version
passed `CCACHE=1` on a runner without ccache and the native build died with `make: ccache: No such file or directory`.
**Status: partial.** The generated recompiled C stays committed (412 MB): moving it to a build artifact would require
the original game image in CI, which the repository cannot legally ship.

---

## Measurement plan (the only place a performance number may come from)

No claim in this document is a measured device result. When a number is needed:

```bash
# per-frame CPU split, 60 s of the same scene on both builds
adb shell simpleperf record -g -p $(adb shell pidof com.protodroid) --duration 60 -o /data/local/tmp/perf.data
adb shell simpleperf report -i /data/local/tmp/perf.data --sort dso,symbol | head -40

# frame timing and jank, from the app's own present instrumentation
python3 tools/perf_evidence.py compare before.log after.log

# one mechanism at a time (the standing rule: one runtime mechanism per device A/B)
```

The A/B protocol is unchanged: one mechanism per build, same scene, same device temperature window, and the result is
reported as a delta with the spread, never as a single best frame.

---

## Review log: what the critic loop actually caught

The critic is not a rubber stamp. Running it over the tree found real defects in this very change, all of which are
fixed above:

| Found | Consequence |
| --- | --- |
| `TouchControls.java` used `Rect` with no `import android.graphics.Rect` | the launcher would not compile |
| `TouchControls.java` used `LinkedHashMap` with no import | the launcher would not compile |
| `ProtoActivity.java` used `GameManager`/`Window` with no imports | the launcher would not compile |
| `ModManager.java` had a duplicated closing brace after the staging cleanup | the rest of the class fell outside the class body |
| `TouchControls.fpsRefresh` invalidated the whole overlay | the statistics band is what changes, so `dirtyStatsRect` |
| dependency flags overrode `CMAKE_C_FLAGS`, losing the NDK target | the dependency job failed (70 s, cold rebuild); flags moved to `CMAKE_*_FLAGS_RELEASE` |
| CMake interprocedural optimisation on SDL2/FAudio | never exercised before; removed, LTO is now `LTO_DEPS=1` and off by default |
| `imports.cpp` kept two unused helpers under `-Werror` | the runtime bridge test caught it, not review |
| `watchdog.cpp` check was an assertion about a log call | the handler was already clean; the *check* was wrong, and it is now scoped to `OnSignal` |
| `SetupActivity.onStop` had a precedence bug in `isFinishing() \|\| … &&` | `isChangingConfigurations()` alone is correct |
| `TouchControls` bitmap ownership double-free | only the shared LRU recycles; the button drops its reference |
| `cached(String, …)` still called with string keys after the signature became `long` | javac: 'String cannot be converted to long' - the stick artwork now uses a numeric key |
| `GameState.GAME_STATE_*` constants | they are the codelab's names; the platform class has `MODE_*` (verified against the API reference) |

The lesson recorded for the next change: a Java edit without a compiler needs a mechanical check in the same commit, and
the check has to be written against what the compiler would actually reject. `tools/test_java_sources.py` now covers
unresolved types, unresolved static access, string arguments into numeric parameters, duplicated methods and `@Override`
mismatches, and `--self-test` proves it reports the two mistakes javac actually caught here.
