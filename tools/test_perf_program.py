#!/usr/bin/env python3
"""Behaviour and shape tests for the performance program (the critic's evidence gate).

Every check here is something that can be decided without a phone: source shape of a change that must not silently
disappear, and the runnable host tests for the pieces that have them (guest allocator, runtime bridge, diagnostics
policy). Device-measured effects are never asserted here - they are reported by the critic as "needs a device A/B".

Run: python3 tools/test_perf_program.py
"""
from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FAILURES: list[str] = []


def text(rel: str) -> str:
    path = ROOT / rel
    if not path.is_file():
        FAILURES.append(f"missing file {rel}")
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def check(condition: bool, message: str) -> None:
    if not condition:
        FAILURES.append(message)


def contains(rel: str, pattern: str, message: str) -> None:
    check(re.search(pattern, text(rel)) is not None, f"{rel}: {message}")


def absent(rel: str, pattern: str, message: str) -> None:
    check(re.search(pattern, text(rel)) is None, f"{rel}: {message}")


def function_body(rel: str, name: str) -> str:
    """Returns the brace-matched body of `name` so a check can be scoped to one function."""
    source = text(rel)
    match = re.search(rf"\b{re.escape(name)}\s*\([^;]*?\)\s*\{{", source)
    if not match:
        FAILURES.append(f"{rel}: cannot find function {name}")
        return ""
    depth, start = 0, match.end() - 1
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    FAILURES.append(f"{rel}: unbalanced braces in {name}")
    return ""


def run(script: list[str], label: str) -> None:
    done = subprocess.run(script, cwd=ROOT, capture_output=True, text=True)
    if done.returncode != 0:
        FAILURES.append(f"{label} failed:\n{(done.stdout + done.stderr)[-1500:]}")


# ---- build and link policy -----------------------------------------------------------------------------------
contains("src/native/Makefile", r"-flto=thin", "ThinLTO is not enabled for the recompiled translation units")
contains("src/native/Makefile", r"thinlto-cache-dir", "the ThinLTO cache directory is not set")
contains("src/native/Makefile", r"version-script", "libmain exports are not restricted by a version script")
contains("src/native/Makefile", r"-fvisibility=hidden", "the build does not hide symbols by default")
absent("src/native/Makefile", r"-Wl,--export-dynamic[^\n]*\n[^\n]*libdobby", "the Android link still exports everything")
check((ROOT / "src/native/libmain.map").is_file(), "the libmain export map is missing")
contains("src/native/libmain.map", r"Java_\*", "JNI entry points are no longer exported")

# Every symbol named explicitly in the version script must actually be defined in the library: ld.lld fails the link
# with "version script assignment of 'global' to symbol 'X' failed: symbol not defined" otherwise, which is exactly how
# JNI_OnLoad/JNI_Unload got caught (libmain.so registers no JNI natives; SDL2 owns them).
exports = re.findall(r"^\s*(?!\*)([A-Za-z_][\w]*)\s*;", text("src/native/libmain.map"), re.MULTILINE)
sources = "\n".join(text(str(p.relative_to(ROOT))) for p in sorted((ROOT / "src/native").glob("*.cpp")))
for symbol in exports:
    check(symbol in sources, f"libmain.map exports {symbol}, which no source in src/native defines")
contains("src/native/Makefile", r"PGO_PROFILE", "the PGO switch is missing")
contains("src/native/Makefile", r"FAST_FP", "the FP-contraction switch is missing")

# ---- recompiled dispatch and imports -------------------------------------------------------------------------
contains("src/native/runtime.cpp", r"extern \"C\" void RecompInit", "the dispatch hash is not built at start-up")
contains("src/native/runtime.cpp", r"RecompFn RecompLookup\(uint32_t target\) \{\n(?:.*\n)*?    if \(!g_hash\) BuildHash\(\);",
         "RecompLookup still takes a lock or a call_once on the hot path")
absent("src/native/runtime.cpp", r"std::call_once\(once, BuildHash\)", "RecompLookup still uses call_once")
contains("src/native/runtime.cpp", r"InlineCacheSlot", "there is no monomorphic inline cache in front of the hash")
contains("src/native/runtime.cpp", r"RecompNameForAddress", "guest symbols cannot be named without the dynamic table")
contains("src/native/imports.cpp", r"const Entry& e = g_entries\[index\]", "import dispatch does not use the dense table")
absent("src/native/imports.cpp", r"static const bool on = SettingInt\(\"import_profile\"",
       "the import profiler still resolves a function-local static per call")

# ---- allocator (items 28-30) ---------------------------------------------------------------------------------
allocator = text("src/native/memory.cpp")
contains("src/native/memory.cpp", r"thread_local Magazines", "guest allocations still serialise on one mutex")
contains("src/native/memory.cpp", r"g_class_lookup", "the size-class lookup is not O(1)")
contains("src/native/memory.cpp", r"kHostPage", "large heap blocks are still rounded to 64 KiB")
contains("src/native/memory.cpp", r"hand its tail", "chunk tails are still abandoned")

# ---- Win32 synchronisation -----------------------------------------------------------------------------------
contains("src/native/kernel32.cpp", r"FUTEX_WAIT_PRIVATE", "event/semaphore waits do not use a per-object futex")
contains("src/native/kernel32.cpp", r"EnterCriticalSectionGuest", "critical sections are still a host mutex map")
absent("src/native/kernel32.cpp", r"thread_local std::unordered_map<uint32_t, std::recursive_mutex\*>",
       "the per-thread critical-section map is still present")
contains("src/native/kernel32.cpp", r"constexpr uint32_t kInline = 8", "WaitForMultipleObjects still allocates every call")
contains("src/native/win.h", r"std::atomic<int> refs", "kernel objects still count references non-atomically")
contains("src/native/kernel32.cpp", r"ReapDeferredLocked", "closed kernel objects are still deleted under a waiter")
contains("src/native/kernel32.cpp", r"pthread_attr_setstacksize", "guest threads still get bionic's default stack")
contains("src/native/kernel32.cpp", r"proto-guest-", "guest threads are not named")
contains("src/native/thread.cpp", r"void DetachThread", "guest thread stacks are never released")
contains("src/native/watchdog.cpp", r"ReportTrace", "the watchdog has no out-of-handler reporting path")
check("Log(" not in function_body("src/native/watchdog.cpp", "OnSignal"),
      "src/native/watchdog.cpp: the signal handler calls Log()")
check("snprintf" not in function_body("src/native/watchdog.cpp", "OnSignal"),
      "src/native/watchdog.cpp: the signal handler formats strings")
check("malloc" not in function_body("src/native/watchdog.cpp", "OnSignal"),
      "src/native/watchdog.cpp: the signal handler allocates")
contains("src/native/thread.cpp", r"PR_SET_TIMERSLACK", "worker wake-ups are not coalesced")

# ---- D3D9 bridge ---------------------------------------------------------------------------------------------
contains("src/native/d3d9.cpp", r"entry = std::move\(it->second\)", "DXT decode still runs under the bridge mutex")
contains("src/native/d3d9.cpp", r"VtableEntry g_vtable_slots", "wrapping an object still builds a std::string")
contains("src/native/d3d9.cpp", r"g_presents_in_flight", "the present mutex is still held across the vsync wait")
contains("src/native/d3d9.cpp", r"CLOCK_THREAD_CPUTIME_ID", "ADPF is still fed the present-to-present interval")
contains("src/native/d3d9.cpp", r"g_hosts\.Release", "D3D9 wrapper indices are never recycled")

# ---- I/O, logging, telemetry ---------------------------------------------------------------------------------
contains("src/native/main.cpp", r"LogWriterLoop", "logging still takes the process-wide lock on the caller")
contains("src/native/main.cpp", r"static bool FileChanged", "the periodic mirror still rewrites unchanged files")
contains("src/native/main.cpp", r'MESA_SHADER_CACHE_DIR', "Turnip's shader cache is still discarded every launch")
contains("src/native/main.cpp", r'ANativeWindow_setFrameRate', "the display is not told the frame rate")
contains("src/native/path.cpp", r"g_case_cache", "path resolution still stats every component on every open")
contains("src/native/kernel32.cpp", r"POSIX_FADV_WILLNEED", "sequential reads still get no readahead hint")

# ---- packaging, launcher, docs -------------------------------------------------------------------------------
contains("android/java/com/protodroid/SetupView.java", r"postInvalidateDelayed\(33\)",
         "the launcher still redraws every vsync forever")
absent("android/AndroidManifest.xml", r"largeHeap", "largeHeap is still set")
contains("android/java/com/protodroid/TouchControls.java", r"dirtyStatsRect",
         "the statistics refresh still invalidates the whole overlay")
contains("android/java/com/protodroid/TouchControls.java", r"buttonStateHash",
         "touch controls still build a string cache key per frame")
contains("android/java/com/protodroid/SetupActivity.java", r"checkAsync",
         "launcher probes still run on the UI thread")
check((ROOT / "docs/perf/67-items.md").is_file(), "the item ledger docs/perf/67-items.md is missing")
check((ROOT / "tools/perf_critic.py").is_file(), "the critic tools/perf_critic.py is missing")

# ---- runnable host tests -------------------------------------------------------------------------------------
run(["bash", "src/native/tests/run_memory_test.sh", "/tmp/protodroid-perf-memory"], "guest allocator test")
run(["bash", "src/native/tests/run_runtime_bridge_test.sh", "/tmp/protodroid-perf-bridge"], "runtime bridge test")
run([sys.executable, "tools/test_runtime_performance.py"], "runtime performance policy test")
run([sys.executable, "tools/test_java_sources.py"], "launcher Java structural verification")
run([sys.executable, "tools/test_java_sources.py", "--self-test"], "launcher Java checker self-test")
run([sys.executable, "tools/test_native_diagnostics.py"], "native diagnostics policy test")

if FAILURES:
    print(f"perf program: {len(FAILURES)} failure(s)")
    for failure in FAILURES:
        print(f"  - {failure}")
    if os.environ.get("GITHUB_ACTIONS") == "true":
        for failure in FAILURES:
            print("::error::perf program: " + failure.splitlines()[0][:900])
    sys.exit(1)
print("perf program checks passed")
