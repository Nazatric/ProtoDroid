#!/usr/bin/env python3
"""Proves the performance critic is not a rubber stamp.

A scoring tool that cannot fail is worthless. This test copies the parts of the tree the critic reads into a temporary
directory, mutates the copy so that a documented claim stops being true, and asserts that the critic's rating falls and
that `--min 8.5` then fails. It checks three different kinds of regression:

  1. a native optimisation disappears (the path-resolution cache is removed),
  2. a *critical* regression comes back (the DXT decode returns under the bridge lock), which must also push the score
     below the 8.5 target - if a critical regression cannot fail the gate, the gate is decoration,
  3. a launcher optimisation disappears (the 30 fps cap on the backdrop),
  4. a ledger justification is deleted (an item that was deferred loses its written reason).

Nothing outside the temporary copy is touched.

Run: python3 tools/test_perf_critic.py
"""
from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COPIED = ("tools/perf_critic.py", "tools/test_perf_program.py", "tools/test_java_sources.py",
          "tools/test_runtime_performance.py", "tools/test_native_diagnostics.py",
          "docs/perf/67-items.md", "src/native", "src/recomp", "android/java", "android/AndroidManifest.xml",
          "android/build-deps.sh", ".github/workflows")
FAILURES: list[str] = []


def rating(root: Path) -> float:
    done = subprocess.run([sys.executable, str(root / "tools" / "perf_critic.py"), "--json"],
                          cwd=root, capture_output=True, text=True,
                          env={**os.environ, "PROTODROID_PERF_ROOT": str(root),
                               "PROTODROID_PERF_SKIP_TESTS": "1"})
    if done.returncode not in (0, 1) or not done.stdout.strip():
        FAILURES.append(f"critic failed to run in {root}: {done.stdout[-500:]}{done.stderr[-500:]}")
        return -1.0
    return float(json.loads(done.stdout)["rating"])


def below_target(root: Path, label: str) -> None:
    done = subprocess.run([sys.executable, str(root / "tools" / "perf_critic.py"), "--min", "8.5"],
                          cwd=root, capture_output=True, text=True,
                          env={**os.environ, "PROTODROID_PERF_ROOT": str(root),
                               "PROTODROID_PERF_SKIP_TESTS": "1"})
    if done.returncode == 0:
        FAILURES.append(f"{label}: the critic still passes --min 8.5 after the regression")


def copy_tree(destination: Path) -> None:
    for entry in COPIED:
        source = ROOT / entry
        target = destination / entry
        target.parent.mkdir(parents=True, exist_ok=True)
        if source.is_dir():
            shutil.copytree(source, target, dirs_exist_ok=True)
        elif source.is_file():
            shutil.copy2(source, target)


def edit(path: Path, pattern: str, replacement: str, label: str) -> None:
    text = path.read_text(encoding="utf-8")
    if re.search(pattern, text) is None:
        FAILURES.append(f"{label}: could not find the code to remove (pattern {pattern!r})")
        return
    path.write_text(re.sub(pattern, replacement, text), encoding="utf-8")


def main() -> int:
    if not (ROOT / "tools" / "perf_critic.py").is_file():
        print("critic is missing")
        return 1

    with tempfile.TemporaryDirectory(prefix="protodroid-critic-") as temp:
        clean = Path(temp) / "clean"
        copy_tree(clean)
        base = rating(clean)
        if base < 8.5:
            FAILURES.append(f"the unmodified tree scores {base}, below the 8.5 target - the checks are too strict")

        # 1. remove the path-resolution cache
        mutated = Path(temp) / "no-case-cache"
        copy_tree(mutated)
        text = (mutated / "src/native/path.cpp").read_text(encoding="utf-8")
        start = text.index("std::string MatchCase(")
        end = text.index("\n}\n", text.index("return found;", start)) + 3
        naive = ("std::string MatchCase(const std::string& dir, const std::string& name) {\n"
                 "    return name;  // mutated: no case resolution, no cache\n}\n")
        mutated_path = text[:start] + naive + text[end:]
        mutated_path = mutated_path.replace(
            "std::unordered_map<CaseCacheKey, std::string, CaseCacheHash> g_case_cache;", "")
        mutated_path = mutated_path.replace("#include <shared_mutex>", "")
        mutated_path = mutated_path.replace("g_case_cache", "g_case_cache_unused")
        (mutated / "src/native/path.cpp").write_text(mutated_path, encoding="utf-8")
        dropped = rating(mutated)
        if dropped >= base:
            FAILURES.append(f"removing the path cache did not lower the rating ({base} -> {dropped})")

        # 1b. put the DXT decode back under the bridge lock (a critical item): this must push the score below target
        mutated = Path(temp) / "decode-under-lock"
        copy_tree(mutated)
        edit(mutated / "src/native/d3d9.cpp",
             r"(?s)(bool AdaptedUnlock\(void\* tex.*?)    AdaptedLock& l = entry;",
             r"\1    DecodeDxt(l.format, Ptr<uint8_t>(l.guest), l.pitch, l.width, l.height, l.host, l.host_pitch,\n"
             r"             l.decode_format);\n    AdaptedLock& l = entry;",
             "decode under lock")
        dropped = rating(mutated)
        if dropped >= base:
            FAILURES.append(f"moving the DXT decode back under the lock did not lower the rating ({base} -> {dropped})")
        below_target(mutated, "decode under the bridge lock")

        # 2. restore the launcher's per-vsync redraw
        mutated = Path(temp) / "launcher-vsync"
        copy_tree(mutated)
        edit(mutated / "android/java/com/protodroid/SetupView.java",
             r"postInvalidateDelayed\(33\)", "postInvalidateDelayed(0)", "launcher redraw")
        dropped = rating(mutated)
        if dropped >= base:
            FAILURES.append(f"removing the launcher frame cap did not lower the rating ({base} -> {dropped})")

        # 3. delete a deferred item's justification
        mutated = Path(temp) / "no-justification"
        copy_tree(mutated)
        edit(mutated / "docs/perf/67-items.md",
             r"(?ms)^### 37\..*?(?=^### )", "", "item 37 justification")
        dropped = rating(mutated)
        if dropped >= base:
            FAILURES.append(f"deleting item 37's justification did not lower the rating ({base} -> {dropped})")

    if FAILURES:
        print(f"critic self-test: {len(FAILURES)} failure(s)")
        for failure in FAILURES:
            print(f"  - {failure}")
        if os.environ.get("GITHUB_ACTIONS") == "true":
            for failure in FAILURES:
                print("::error::critic self-test: " + failure.splitlines()[0][:900])
        return 1
    print("critic self-test passed: the rating falls when a documented claim stops being true")
    return 0


if __name__ == "__main__":
    sys.exit(main())
