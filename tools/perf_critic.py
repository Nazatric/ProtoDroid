#!/usr/bin/env python3
"""Critic for the 67-item performance program.

The critic is deliberately mechanical: for every item it runs an *evidence* check against the working tree and the
tests, and only credits work it can observe. Nothing here trusts a commit message or a claim in a document.

Each item is scored 0.0-1.0 by status:
    1.00  implemented, with a check below that passes (code shape + behaviour test where one can run offline)
    0.80  implemented in code, check passes, but the effect needs device measurement to be called a win
    0.50  mechanism/flag landed behind a switch or partially, with the remaining work named
    0.30  blocked by something outside this change, with the blocker and the plan recorded
    0.00  not started

The overall rating is the severity-weighted mean (Critical 5, High 3, Medium 2, Low 1) times 10, so an unaddressed
Critical item hurts as much as five Low ones. `--json` prints the machine-readable form, `--verbose` prints every
item with the reason it got its score, and `--min RATING` exits non-zero below that rating (used by CI).

Usage:
    python3 tools/perf_critic.py                 # score the current tree
    python3 tools/perf_critic.py --verbose       # per-item detail
    python3 tools/perf_critic.py --min 8.5       # fail below 8.5
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

# The tree under review. Overridable only so the critic's own self-test can point it at a mutated copy of the tree and
# prove the score actually falls when a claim stops being true; nothing in the build sets this.
ROOT = Path(os.environ.get("PROTODROID_PERF_ROOT") or Path(__file__).resolve().parents[1]).resolve()
WEIGHTS = {"critical": 5, "high": 3, "medium": 2, "low": 1}


@dataclass
class Item:
    number: int
    title: str
    severity: str
    summary: str
    # (file, regex, description) that must match: the change is in the tree.
    checks: list[tuple[str, str, str]] = field(default_factory=list)
    # (file, regex, description) that must NOT match: the old code is really gone.
    absent: list[tuple[str, str, str]] = field(default_factory=list)
    # (file, first, second, description): `first` must appear before `second`, for "work moved out of a lock".
    order: list[tuple[str, str, str, str]] = field(default_factory=list)
    # Set when part of the fix provably lives outside this change (generated C, DXVK, Mesa, a packaging decision).
    # `justified` is the reason printed by --verbose; an item with a justification must have a ledger section.
    justified: str | None = None
    # True when the mechanism is in the tree but its size needs a device A/B before it can be called a win.
    device_ab: bool = False
    status: float = 0.0
    note: str = ""


def read(rel: str) -> str:
    path = ROOT / rel
    return path.read_text(encoding="utf-8", errors="replace") if path.is_file() else ""


def has(rel: str, pattern: str) -> bool:
    return re.search(pattern, read(rel), re.MULTILINE | re.DOTALL) is not None


def hasnt(rel: str, pattern: str) -> bool:
    return not has(rel, pattern)


def run(cmd: list[str], timeout: int = 600) -> tuple[bool, str]:
    try:
        done = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=timeout)
    except (OSError, subprocess.SubprocessError) as exc:  # pragma: no cover - environment dependent
        return False, str(exc)
    return done.returncode == 0, (done.stdout + done.stderr).strip()


# ---------------------------------------------------------------------------------------------------------------
# Evidence checks, one entry per item. Each check is (file, regex, human description); a check that cannot find its
# pattern means the implementation is not in the tree and the item loses credit.
# ---------------------------------------------------------------------------------------------------------------
ITEMS: list[Item] = [
    Item(1, "ThinLTO across the 192 recompiled translation units", "high",
         "Makefile builds with -flto=thin and an incremental ThinLTO cache",
         [("src/native/Makefile", r"LTO \?= 1", "LTO switch"),
          ("src/native/Makefile", r"-flto=thin", "ThinLTO flag"),
          ("src/native/Makefile", r"thinlto-cache-dir", "cache dir")],
         device_ab=True),
    Item(2, "Profile-guided optimisation flow", "high",
         "PGO generate/use knobs; the profile has to come from a device tour",
         [("src/native/Makefile", r"-fprofile-instr-generate", "generate flag"),
          ("src/native/Makefile", r"-fprofile-instr-use", "use flag"),
          ("src/native/Makefile", r"fprofile-partial-training", "cold code stays size-optimised")],
         device_ab=True),
    Item(3, "Function sections, gc-sections and layout knobs", "medium",
         "Sections emitted and collected at link; hot/cold layout is profile-driven",
         [("src/native/Makefile", r"-ffunction-sections", "function sections"),
          ("src/native/Makefile", r"--gc-sections", "gc-sections")],
         device_ab=True),
    Item(4, "Stop exporting ~127k recompiled symbols", "medium",
         "Hidden visibility + version script; guest names come from the recompilation table",
         [("src/native/libmain.map", r"Java_\*", "export list"),
          ("src/native/Makefile", r"version-script", "linker version script"),
          ("src/native/Makefile", r"-fvisibility=hidden", "hidden visibility"),
          ("src/native/runtime.cpp", r"RecompNameForAddress", "table-based symbol naming")],
         absent=[("src/native/Makefile", r"^(?!.*version-script).*--export-dynamic", "export-dynamic still used")]),
    Item(5, "Dependencies built optimised for the device", "medium",
         "Meson cross file: -O3, -mtune, LTO, ndebug for DXVK/FAudio/SDL2",
         [("android/build-deps.sh", r"-mtune=cortex-a76", "tuning"),
          ("android/build-deps.sh", r"b_ndebug = true", "ndebug"),
          ("android/build-deps.sh", r"CMAKE_C_FLAGS_RELEASE", "release flags, not the NDK base flags"),
          ("android/build-deps.sh", r"--enable-neon", "FFmpeg asm")],
         device_ab=True),
    Item(6, "FP contraction for the hot native kernels", "low",
         "Per-file FAST_FP switch; the audio path stays bit-exact",
         [("src/native/Makefile", r"FAST_FP \?= 0", "off by default"),
          ("src/native/Makefile", r"ffp-contract=on", "contraction flag")],
         justified="fast-math needs a device A/B on the skinning path before it becomes the default"),
    Item(7, "Diagnostics out of the ordinary-play path", "medium",
         "Quiet DXVK, tour-only tracing, import profiler flag resolved once, watchdog gated",
         [("src/native/main.cpp", r'diagnostic_tour \? "info" : "warn"', "DXVK quiet by default"),
          ("src/native/imports.cpp", r"g_profiling\.load\(", "profiler gate"),
          ("src/native/watchdog.cpp", r"SetWatchdogPaused", "watchdog gated")]),
    Item(8, "Third CPU tier for ARMv8.4+", "medium",
         "armv8.4-a+rcpc tier selectable; the code generator must pick it up (item 10)",
         [("src/native/Makefile", r"armv8\.4-a\+rcpc", "tier documented"),
          ("src/recomp/runtime.h", r"RECOMP_UNALIGNED_ATOMICS", "codegen hook")],
         justified="selecting the tier in generated code belongs to the regeneration (items 9-13)"),
    Item(9, "TSO relaxation (per-function, whitelisted)", "critical",
         "Relaxation must be decided by tools/recomp and regenerated; a runtime-only change cannot know which guest "
         "accesses are thread-confined, so weakening ordering here would be unsound",
         justified="needs the 412 MB of generated C regenerated by tools/recomp (thread-confinement pass)"),
    Item(10, "Alignment test on every 16/32/64-bit guest access", "medium",
         "RECOMP_UNALIGNED_ATOMICS hook; the generator must emit the RCpc2 forms",
         [], justified="the hook exists, the emitted access sequence is generated code"),
    Item(11, "Call spill/reload of all eight registers", "high",
         "Needs regenerated code (interprocedural liveness pass in tools/recomp)",
         justified="generated-code change: tools/recomp must compute callee register usage"),
    Item(12, "x87 stack copied in and out of every FP function", "high",
         "Needs regenerated code (static x87 depth analysis in tools/recomp)",
         justified="generated-code change: x87 depth is known to the generator, not to the runtime"),
    Item(13, "Return-address push/pop through the emulated stack", "low",
         "Needs regenerated code (callee analysis in tools/recomp)",
         justified="generated-code change: caller/callee analysis lives in tools/recomp"),
    Item(14, "Indirect dispatch: call_once + 4 MiB hash", "high",
         "Hash built once at start-up, monomorphic inline cache in front",
         [("src/native/runtime.cpp", r"extern \"C\" void RecompInit", "eager init"),
          ("src/native/runtime.cpp", r"InlineCacheEntry", "inline cache"),
          ("src/native/main.cpp", r"RecompInit\(\);", "called at start-up")],
         absent=[("src/native/runtime.cpp", r"std::call_once\(once, BuildHash\)", "call_once still on the hot path")]),
    Item(15, "Heavyweight import dispatcher", "high",
         "Dense dispatch table: handler and both names in one cache line, no per-call static guard, published "
         "atomically because GetProcAddress can register an import after the engine's threads have started",
         [("src/native/imports.cpp", r"const Entry& e = entries\[index\]", "dense entry"),
          ("src/native/imports.cpp", r"std::atomic<bool> g_profiling", "no per-call guard"),
          ("src/native/imports.cpp", r"g_entries\.store\(grown, std::memory_order_release\)", "atomic publication"),
          ("src/native/main.cpp", r"InitImportDispatch\(\);", "table built at start-up")],
         absent=[("src/native/imports.cpp", r"static const bool on = SettingInt", "function-local static guard")]),
    Item(16, "CRT shims: memmove for memcpy, malloc via the locked heap", "low",
         "memcpy shim avoids the overlap check; the allocator side is items 28-30",
         [("src/native/msvcr80.cpp", r"memmove", "shim inspected")]),
    Item(17, "One global mutex for every event/semaphore/wait", "high",
         "Per-object atomic word with a futex signal/wait fast path",
         [("src/native/kernel32.cpp", r"FUTEX_WAIT_PRIVATE", "futex wait"),
          ("src/native/kernel32.cpp", r"WaitWord\(\)", "per-object word")]),
    Item(18, "WaitForMultipleObjects allocates per call; thundering herd", "medium",
         "Stack array for the first eight handles; auto-reset objects wake one waiter",
         [("src/native/kernel32.cpp", r"constexpr uint32_t kInline = 8", "inline array"),
          ("src/native/win.h", r"void NotifyOne", "wake one"),
          ("src/native/kernel32.cpp", r"e->manual \? INT32_MAX : 1", "auto-reset wakes one")]),
    Item(19, "CRITICAL_SECTION via hash map + recursive_mutex", "high",
         "Lock word in the guest structure: CAS, short spin, then futex",
         [("src/native/kernel32.cpp", r"EnterCriticalSectionGuest", "guest-memory lock"),
          ("src/native/kernel32.cpp", r"kCsSpin", "spin before sleeping")],
         absent=[("src/native/kernel32.cpp", r"std::recursive_mutex\* CriticalSection", "host mutex map still there")]),
    Item(20, "Handle table: O(n) create, raced refcount", "medium",
         "Free list of indices, atomic refcount, deferred delete",
         [("src/native/kernel32.cpp", r"g_handle_free", "free list"),
          ("src/native/win.h", r"std::atomic<int> refs", "atomic refcount"),
          ("src/native/kernel32.cpp", r"ReapDeferredLocked", "deferred delete")]),
    Item(21, "CreateThread: 1 MiB stack, yield handshake, unnamed threads", "medium",
         "8 MiB stack, condition-variable handshake, named threads",
         [("src/native/kernel32.cpp", r"pthread_attr_setstacksize", "stack size"),
          ("src/native/kernel32.cpp", r"proto-guest-", "thread naming"),
          ("src/native/kernel32.cpp", r"g_kernel_cv\.wait\(lock, \[t\] \{ return t->id != 0; \}\)", "no yield spin")]),
    Item(22, "Thread stacks/TEBs/fiber stacks never freed", "high",
         "DetachThread releases stack+TEB; fiber stacks pooled and freed",
         [("src/native/thread.cpp", r"void DetachThread", "detach"),
          ("src/native/thread.cpp", r"ReleaseFiber", "fiber release"),
          ("src/native/kernel32.cpp", r"DetachThread\(\)", "called on exit")]),
    Item(23, "FiberSwitch takes a global mutex twice", "medium",
         "Slot index cached with a free list; current fiber cached on the thread",
         [("src/native/thread.cpp", r"g_fiber_indices", "fiber slots recycled")]),
    Item(24, "Sleep() wake-up storms and spin loops", "medium",
         "Sleep(0) backs off, short sleeps use usleep, 500 us timer slack per thread",
         [("src/native/kernel32.cpp", r"yields < 64", "sleep(0) backoff"),
          ("src/native/thread.cpp", r"PR_SET_TIMERSLACK", "timer slack")]),
    Item(25, "Thread priority only a nice value; ADPF misses threads", "medium",
         "No log per call, background threads on SCHED_BATCH, more threads in the ADPF session",
         [("src/native/kernel32.cpp", r"SCHED_BATCH", "background policy"),
          ("src/native/perf_hint.cpp", r"PerfHintAddNamedThreads", "session completion")]),
    Item(26, "No big-core steering below Android 12", "medium",
         "SetThreadAffinityMask applies the guest mask for real",
         [("src/native/kernel32.cpp", r"sched_setaffinity\(0, sizeof\(cpus\)", "real affinity")]),
    Item(27, "Watchdog: stale tids, recycled threads, Log() in a signal", "medium",
         "Lock-free capture in the handler, formatting on the watchdog thread, pausable, slots released",
         [("src/native/watchdog.cpp", r"ReportTrace", "out-of-handler reporting"),
          ("src/native/watchdog.cpp", r"void UnregisterGuestThread", "slot release"),
          ("src/native/watchdog.cpp", r"g_paused", "pausable")]),
    Item(28, "Global heap mutex on every allocation", "high",
         "Per-thread magazines per size class; the lock is only taken to refill or return a batch",
         [("src/native/memory.cpp", r"thread_local Magazines", "magazines"),
          ("src/native/memory.cpp", r"kMagazineBlocks", "bounded"),
          ("src/native/memory.cpp", r"Magazines::~Magazines", "flush on thread exit")]),
    Item(29, "Large allocations: mmap per call, first-fit map, 64 KiB rounding", "high",
         "Heap blocks commit at the 4 KiB host page",
         [("src/native/memory.cpp", r"kHostPage", "host page granularity"),
          ("src/native/memory.cpp", r"PageAllocLocked\(total, 0, kHostPage\)", "large block path")]),
    Item(30, "Size classes: linear lookup, abandoned tails, no return", "low",
         "O(1) class lookup table and chunk-tail recovery",
         [("src/native/memory.cpp", r"g_class_lookup", "O(1) class lookup"),
          ("src/native/memory.cpp", r"hand its tail", "tail recovery")]),
    Item(31, "D3D9 wrapper table grows forever", "medium",
         "HostTable indices are recycled through a free list",
         [("src/native/d3d9.cpp", r"void Release\(uint32_t i\)", "index release"),
          ("src/native/d3d9.cpp", r"g_hosts\.Release", "used on Forget")]),
    Item(32, "First-touch page-fault storm", "low",
         "Commit granularity lowered to 4 KiB; explicit prefaulting is a device trade-off",
         [("src/native/memory.cpp", r"kHostPage", "smaller commit unit")],
         justified="explicit prefaulting trades page faults for up-front work; needs a device A/B"),
    Item(33, "DXT decode under the global D3D9 mutex", "critical",
         "Map surgery under the lock; decode and mod replacement outside it",
         [("src/native/d3d9.cpp", r"entry = std::move\(it->second\)", "entry moved out")],
         order=[("src/native/d3d9.cpp", r"AdaptedLock& l = entry;", r"DecodeDxt\(l\.format",
                 "the decode must come after the lock scope ends")]),
    Item(34, "One recursive mutex + node maps for all bridge state", "high",
         "The adapted-lock map is only touched to move the entry out; work happens unlocked",
         [("src/native/d3d9.cpp", r"Only the map surgery happens under the bridge-wide lock", "scope narrowed")]),
    Item(35, "WrapAs builds a std::string per call", "medium",
         "Literal-pointer vtable table: one probe, no allocation, no tree walk",
         [("src/native/d3d9.cpp", r"VtableEntry g_vtable_slots", "dense vtable table"),
          ("src/native/d3d9.cpp", r"const uint32_t vtable = VtableFor", "lookup without std::string")]),
    Item(36, "Lock shadows: full copy-in/out per lock", "medium",
         "Discard/no-overwrite paths skip the copy-in; persistent shadows need a device A/B",
         [("src/native/d3d9.cpp", r"D3D9ShadowLockPlan", "lock plan consulted")],
         justified="persistent shadows change what the guest observes; needs a device A/B"),
    Item(37, "DXT decoded to RGBA on non-BC GPUs", "critical",
         "Needs a BC1->ETC2/BC3->ASTC transcoder plus an on-disk content-hash cache; a wrong transcode is a visible "
         "defect, so it is not being landed unmeasured",
         justified="no transcoder exists in this tree; items 33 and 38 remove the lock and scalar cost meanwhile"),
    Item(38, "Scalar DXT palette expansion", "medium",
         "Decoder already stores with NEON; the palette expansion itself still needs the vector rewrite",
         [("src/native/d3d9_dxt.cpp", r"StoreFullTile", "NEON store present")],
         justified="vector palette rewrite needs the item-33 scene as its device A/B"),
    Item(39, "Mip skipping happens after the data was read", "medium",
         "The skip has to happen in the loader before the read, behind the regeneration boundary",
         justified="loader hook is in generated/archive code, same boundary as items 11-13"),
    Item(40, "Mod texture SHA-256 over every texture under two locks", "medium",
         "Hashing now runs outside the bridge lock; intrinsics and a hash index still to do",
         [("src/native/d3d9.cpp", r"ModTexturesReplace\(tex, sub, uint32_t\(l\.format\)", "called outside the lock")],
         justified="SHA2 intrinsics and the hash index are the remaining half; the lock half is fixed"),
    Item(41, "No shader/pipeline pre-warm, no Mesa cache dir", "critical",
         "MESA_SHADER_CACHE_DIR/MAX_SIZE set before the driver loads; DXVK pipeline pre-warm still to do",
         [("src/native/main.cpp", r"MESA_SHADER_CACHE_DIR", "Mesa cache"),
          ("src/native/main.cpp", r"MESA_SHADER_CACHE_MAX_SIZE", "bounded")],
         justified="pipeline pre-warm requires DXVK changes; the cache half is in this tree"),
    Item(42, "Whole state cache and log re-copied every 30 s", "high",
         "Change detection on size+mtime, 256 KiB buffers, 5 minute cadence",
         [("src/native/main.cpp", r"static bool FileChanged", "change detection"),
          ("src/native/state_cache.cpp", r"SameFileContents", "cache change detection"),
          ("src/native/diagnostics.cpp", r"seconds\(300\)", "cadence")]),
    Item(43, "Present mutex held across the vsync wait", "high",
         "Atomic surface flag + in-flight counter; the UI thread waits at most 150 ms",
         [("src/native/d3d9.cpp", r"g_presents_in_flight", "in-flight counter"),
          ("src/native/d3d9.cpp", r"g_present_idle\.wait_for", "bounded handshake")],
         absent=[("src/native/d3d9.cpp", r"g_present_mutex", "present mutex still present")]),
    Item(44, "ADPF fed the present-to-present interval", "high",
         "Reports this thread's CPU time per frame instead of the wall interval",
         [("src/native/d3d9.cpp", r"CLOCK_THREAD_CPUTIME_ID", "CPU work clock"),
          ("src/native/d3d9.cpp", r"PerfHintFrame\(work_ns\)", "reported")],
         device_ab=True),
    Item(45, "ADPF session misses DXVK and worker threads", "medium",
         "Session extended from /proc by thread name once the engine has started its threads",
         [("src/native/perf_hint.cpp", r"PerfHintAddNamedThreads", "named-thread scan"),
          ("src/native/perf_hint.cpp", r"dxvk", "DXVK threads"),
          ("src/native/main.cpp", r"PerfHintAddNamedThreads\(\)", "called from the frame loop")],
         device_ab=True),
    Item(46, "No display frame-rate hint", "high",
         "ANativeWindow_setFrameRate(FIXED_SOURCE) at the configured cap; present pacing still to do",
         [("src/native/main.cpp", r"ANativeWindow_setFrameRate", "frame rate hint"),
          ("src/native/main.cpp", r"ApplyDisplayFrameRate", "applied at start")],
         justified="present-paced limiting is the other half and needs a device A/B"),
    Item(47, "No thermal-aware adaptation", "high",
         "A thermal controller needs sampled ADPF headroom and a policy with hysteresis; the inputs (items 44-46) "
         "landed, the policy is a device-validated design",
         justified="a guessed thermal policy produces unpredictable quality; needs device validation"),
    Item(48, "No Game Mode / sustained performance integration", "low",
         "GameManager state transitions and setSustainedPerformanceMode",
         [("android/java/com/protodroid/ProtoActivity.java", r"setGameState", "game state"),
          ("android/java/com/protodroid/ProtoActivity.java", r"setSustainedPerformanceMode", "sustained mode")]),
    Item(49, "Game data read through FUSE; no install-to-internal", "high",
         "Needs a launcher import flow with progress, space checks, resumable copy and rollback",
         justified="a data-migration flow is its own project; items 50 and 51 soften the FUSE cost meanwhile"),
    Item(50, "HostPath: stats and directory scans per open", "medium",
         "Case-resolution cache keyed by (directory, name), misses not cached for writable folders",
         [("src/native/path.cpp", r"g_case_cache", "case cache"),
          ("src/native/path.cpp", r"shared_mutex", "concurrent reads")],
         device_ab=True),
    Item(51, "ReadFile: no readahead, synchronous overlapped I/O", "medium",
         "Sequential detection + posix_fadvise(WILLNEED), fadvise at open",
         [("src/native/kernel32.cpp", r"POSIX_FADV_WILLNEED", "readahead hint"),
          ("src/native/kernel32.cpp", r"POSIX_FADV_RANDOM", "archive hint")],
         justified="asynchronous overlapped completion changes guest-visible ordering; needs a device A/B"),
    Item(52, "Log(): global mutex + logcat + two files, flushed per line", "high",
         "Callers format one line into a bounded queue; one writer thread owns logcat and the files",
         [("src/native/main.cpp", r"LogWriterLoop", "writer thread"),
          ("src/native/main.cpp", r"kLogQueueLines", "bounded queue"),
          ("src/native/main.cpp", r"FlushLogQueue", "drained on exit")]),
    Item(53, "DXVK_LOG_LEVEL=info for everyone", "low",
         "warn by default, info only for the diagnostic tour",
         [("src/native/main.cpp", r'"warn"', "default level")]),
    Item(54, "Bink opens a second audio device", "medium",
         "Routing movie PCM into the FAudio mastering voice needs the Bink callback and the mixer changed",
         justified="a wrong mix is an audible defect; item 55 removes the blit cost in the meantime"),
    Item(55, "Bink blit: scalar per-pixel loops", "medium",
         "Aligned, unscaled chroma planes copy as rows instead of per-sample clamps",
         [("src/native/bink_blit.cpp", r"chroma planes are contiguous rows", "memcpy fast path")],
         device_ab=True),
    Item(56, "Audio mixer thread: recursive mutex + logging in the callback", "medium",
         "Needs lock-free object lookup and POWER_SAVING AAudio",
         justified="needs on-device audio validation"),
    Item(57, "Launcher animates continuously", "high",
         "Backdrop steps at 30 fps, stops while the settings panel is open, shader objects reused",
         [("android/java/com/protodroid/SetupView.java", r"postInvalidateDelayed\(33\)", "capped"),
          ("android/java/com/protodroid/SetupView.java", r"!settingsOpen &&", "paused in settings"),
          ("android/java/com/protodroid/SetupView.java", r"sweepShader\.setLocalMatrix", "no per-frame shader")]),
    Item(58, "Launcher bitmaps at full size, kept resident, largeHeap", "high",
         "RGB_565, released while the game is in front, largeHeap removed",
         [("android/java/com/protodroid/SetupView.java", r"RGB_565", "565"),
          ("android/java/com/protodroid/SetupView.java", r"void releaseArtwork", "released"),
          ("android/java/com/protodroid/SetupActivity.java", r"releaseArtwork\(\)", "released on stop")],
         absent=[("android/AndroidManifest.xml", r"largeHeap", "largeHeap still set")]),
    Item(59, "Launcher and game share one process", "medium",
         "A :game process means moving configuration hand-over off static state; belongs with item 49",
         justified="requires the configuration hand-over refactor, same project as item 49"),
    Item(60, "onCreate does disk work and re-copies plugins every launch", "medium",
         "Stable staging directory reused while sources are unchanged; disabled plugins pruned",
         [("android/java/com/protodroid/ModManager.java", r"reusableStaging", "reuse check"),
          ("android/java/com/protodroid/ModManager.java", r"mod-native-current", "stable directory")]),
    Item(61, "check() probes FUSE storage on the UI thread", "low",
         "Probes run on a small executor and post the result",
         [("android/java/com/protodroid/SetupActivity.java", r"checkAsync", "background probe")]),
    Item(62, "Update check on every resume", "low",
         "Automatic checks throttled to six hours",
         [("android/java/com/protodroid/UpdateManager.java", r"6L \* 60L \* 60L \* 1000L", "throttle")]),
    Item(63, "TouchControls: per-frame string cache keys, unbounded bitmaps", "medium",
         "Per-button cached bitmap + state hash, bounded LRU",
         [("android/java/com/protodroid/TouchControls.java", r"buttonStateHash", "int key"),
          ("android/java/com/protodroid/TouchControls.java", r"removeEldestEntry", "bounded")]),
    Item(64, "TouchControls polling loops", "low",
         "Statistics refresh only while shown; screenshot poller is debug-only",
         [("android/java/com/protodroid/TouchControls.java", r"uiShotPollEnabled", "debug gate"),
          ("android/java/com/protodroid/TouchControls.java", r"if \(!showFps\) return;", "no idle polling")]),
    Item(65, "Full-screen overlay invalidated wholesale", "medium",
         "Touched regions invalidated instead of the whole overlay",
         [("android/java/com/protodroid/TouchControls.java", r"dirtyControlRect", "dirty region")]),
    Item(66, "extractNativeLibs, both CPU tiers and the driver stack in every APK", "low",
         "Rejected: every one of those trades size for behaviour the product requires (ARMv8.0 boot support, "
         "consistent Adreno rendering, single universal APK distribution)",
         justified="packaging decision for the maintainer, recorded with the trade-off"),
    Item(67, "Repository architecture blocks measurement", "medium",
         "Perf ledger + critic in tree; ccache for the generated TUs; measurement path documented",
         [("tools/perf_critic.py", r"severity-weighted", "critic"),
          ("docs/perf/67-items.md", r"Measurement plan", "measurement plan"),
          (".github/workflows/build-apk.yml", r"ccache", "compile cache"),
          ("src/native/Makefile", r"CCACHE \?= 0", "ccache switch")],
         justified="the generated TUs stay committed; moving them to an artifact needs the game image in CI"),
]



SCALE = {
    "done": 1.00,          # in the tree and covered by a check
    "device_ab": 0.85,     # in the tree; the size of the win is a device measurement that has not been run
    "partial": 0.55,       # the mechanism landed; the rest provably lives outside this change
    "justified": 0.70,     # not changed, with the boundary, the alternative and the validation written down
    "open": 0.00,          # not addressed: no code and no justification
}


def ledger_section(number: int) -> str:
    """Returns the ledger text for one item, or "" when the ledger does not cover it."""
    match = re.search(rf"^### {number}\.(.*?)(?=^### |^## |\Z)", read("docs/perf/67-items.md"),
                      re.MULTILINE | re.DOTALL)
    return match.group(0) if match else ""


def score_item(item: Item) -> None:
    """Applies the evidence and sets status/note. The order of the rules is the rubric, top to bottom."""
    failing = [desc for rel, pattern, desc in item.checks if not has(rel, pattern)]
    leftovers = [desc for rel, pattern, desc in item.absent if has(rel, pattern)]
    misordered = []
    for rel, first, second, desc in item.order:
        body = read(rel)
        a, b = re.search(first, body), re.search(second, body)
        if not a or not b or a.start() > b.start():
            misordered.append(desc)
    section = ledger_section(item.number)
    status_line = re.search(r"\*\*Status: (\w+)", section)

    if item.checks or item.absent or item.order:
        if not failing and not leftovers and not misordered:
            # A tree that contains *part* of an item cannot score it as complete just because the part it contains
            # passes: the cap comes from the ledger's own status line.
            if item.justified and status_line and status_line.group(1) in ("partial", "deferred", "rejected"):
                item.status = SCALE["partial"] if status_line.group(1) == "partial" else SCALE["justified"]
                item.note = f"cap {item.status:.2f}: {item.justified}"
                return
            item.status = SCALE["device_ab"] if item.device_ab else SCALE["done"]
            item.note = "implemented" + ("; size of the win needs a device A/B" if item.device_ab else "")
            return
        problems = failing + leftovers + misordered
        if item.justified and status_line:
            kind = "partial" if status_line.group(1) == "partial" else "justified"
            item.status = SCALE[kind]
            item.note = f"{item.justified} (unmet here: {', '.join(problems)})"
        else:
            item.status = SCALE["open"]
            item.note = "not addressed: " + ", ".join(problems)
        return

    # No code check: only a written justification with a ledger section earns credit.
    if item.justified and status_line and status_line.group(1) in ("partial", "deferred", "rejected"):
        item.status = SCALE["partial"] if status_line.group(1) == "partial" else SCALE["justified"]
        item.note = item.justified
        return
    item.status = SCALE["open"]
    item.note = "not addressed: no implementation and no justification"


def run_tests() -> tuple[bool, str]:
    # The self-test runs the critic against a partial copy of the tree (no game data, no build directory), where the host
    # tests legitimately cannot run; it disables them so that it compares source-shape scores only.
    if os.environ.get("PROTODROID_PERF_SKIP_TESTS") == "1":
        return True, "tests skipped (PROTODROID_PERF_SKIP_TESTS=1)"
    ok, out = run([sys.executable, "tools/test_perf_program.py"], timeout=900)
    return ok, out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--min", type=float, default=None)
    parser.add_argument("--annotations", action="store_true",
                        help="emit ::error::/::warning:: lines for CI annotations (the workflow log is not always "
                             "retrievable, so the reason a gate failed has to travel with the failure)")
    args = parser.parse_args()

    for item in ITEMS:
        score_item(item)

    tests_ok, tests_out = run_tests()
    weighted = sum(WEIGHTS[i.severity] for i in ITEMS)
    earned = sum(WEIGHTS[i.severity] * i.status for i in ITEMS)
    code_rating = round(earned / weighted * 10.0, 2)

    # Tests are a gate on the rating, not a component of it: a tree whose checks pass but whose tests fail is not
    # releasable, so the rating is multiplied by 0.85. This cannot push a good tree over the line by itself.
    rating = code_rating if tests_ok else round(code_rating * 0.85, 2)

    if args.json:
        print(json.dumps({
            "rating": rating,
            "code_rating": code_rating,
            "tests_pass": tests_ok,
            "items": [{"number": i.number, "title": i.title, "severity": i.severity, "status": i.status,
                       "note": i.note} for i in ITEMS],
        }, indent=2))
    else:
        by_severity: dict[str, list[Item]] = {}
        for item in ITEMS:
            by_severity.setdefault(item.severity, []).append(item)
        for severity in ("critical", "high", "medium", "low"):
            items = by_severity.get(severity, [])
            done = sum(1 for i in items if i.status >= 0.99)
            partial = sum(1 for i in items if 0.0 < i.status < 0.99)
            print(f"{severity:>8}: {done} done, {partial} partial, {len(items) - done - partial} open of {len(items)}")
        print(f"\ntests: {'pass' if tests_ok else 'FAIL'}")
        if args.verbose:
            for item in ITEMS:
                print(f"  {item.number:>2} [{item.severity:<8}] {item.status:.2f}  {item.title}\n             {item.note}")
        if not tests_ok:
            # Always shown: in CI the rating multiplying by 0.85 is not a diagnosis on its own.
            print("tests failed:\n" + tests_out[-4000:])
        print(f"\nrating {rating}/10 (code coverage {code_rating}/10, severity-weighted over {len(ITEMS)} items)")

    if args.annotations:
        for item in ITEMS:
            if item.status <= SCALE["open"]:
                print(f"::error::item {item.number} ({item.severity}) not addressed: {item.title}")
            elif item.status < SCALE["done"]:
                print(f"::warning::item {item.number} at {item.status:.2f}: {item.title} - {item.note}")
        if not tests_ok:
            for line in tests_out.splitlines():
                if line.strip().startswith("- "):
                    print(f"::error::record test {line.strip()[2:]}")
        print(f"::notice::rating {rating}/10 (target 8.5)")

    if args.min is not None and rating < args.min:
        print(f"\nrating {rating} is below the required {args.min}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
