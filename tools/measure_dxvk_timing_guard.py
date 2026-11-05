#!/usr/bin/env python3
"""Measure the exact disabled DXVK D3D9Timing::Enabled() header on a native host."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess

from measure_d3d9_bridge import benchmark

ROOT = Path(__file__).resolve().parents[1]

LOG_STUB = """#pragma once
#include <string>
namespace dxvk {
class Logger {
public:
    static void info(const std::string&);
};
}
"""

ENV_STUB = """#pragma once
#include <string>
namespace dxvk::env {
std::string getEnvVar(const char* name);
}
"""

HARNESS = r'''
#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "d3d9_timing.h"

namespace dxvk { void Logger::info(const std::string&) {} }
namespace dxvk::env { std::string getEnvVar(const char*) { return ""; } }

__attribute__((noinline)) uint64_t ProbedOne(uint64_t x) {
    return x + (dxvk::D3D9Timing::Enabled() ? 1 : 0);
}
__attribute__((noinline)) uint64_t ControlOne(uint64_t x) { return x; }
using Fn = uint64_t (*)(uint64_t);

__attribute__((noinline)) uint64_t Run(Fn fn, uint64_t n, uint64_t* checksum) {
    uint64_t x = 0x9e3779b97f4a7c15ull;
    const auto begin = std::chrono::steady_clock::now();
    for (uint64_t i = 1; i <= n; ++i) x = fn((x ^ i) * 0xbf58476d1ce4e5b9ull);
    const auto end = std::chrono::steady_clock::now();
    *checksum = x;
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
}

int main(int argc, char** argv) {
    const uint64_t calls = argc > 1 ? std::strtoull(argv[1], nullptr, 0) : 20000000;
    if (!calls) return 2;
    uint64_t ignored = 0;
    Run(ProbedOne, 100000, &ignored);  // Complete lazy environment/guard initialization before timing.
    uint64_t expected = 0;
    Run(ControlOne, calls, &expected);  // Same arithmetic without a guard; untimed correctness control.
    uint64_t sum = 0;
    const uint64_t elapsed = Run(ProbedOne, calls, &sum);
    std::printf("{\"calls\":%" PRIu64 ",\"elapsed_ns\":%" PRIu64 ",\"sum\":%" PRIu64 "}\n",
                calls, elapsed, sum);
    return sum == expected ? 0 : 6;
}
'''


def extract_header(patch: Path) -> str:
    lines = patch.read_text(encoding="utf-8").splitlines()
    start = lines.index("--- /dev/null") + 2
    if not lines[start - 1].startswith("+++ b/src/d3d9/d3d9_timing.h"):
        raise ValueError(f"{patch}: first added file is not d3d9_timing.h")
    body = []
    for line in lines[start:]:
        if line.startswith("--- a/") or line.startswith("diff --git "):
            break
        if line.startswith("@@") or not line.strip() or line.startswith("\\ No newline"):
            continue
        if not line.startswith("+") or line.startswith("+++"):
            raise ValueError(f"{patch}: unexpected added-header line {line!r}")
        body.append(line[1:])
    text = "\n".join(body) + "\n"
    if "class D3D9Timing" not in text or "static bool Enabled()" not in text:
        raise ValueError(f"{patch}: incomplete timing header")
    return text


def compile_variant(header: str, output: Path, name: str) -> Path:
    tree = output / name / "src"
    (tree / "d3d9").mkdir(parents=True)
    (tree / "util/log").mkdir(parents=True)
    (tree / "util").mkdir(parents=True, exist_ok=True)
    (tree / "d3d9/d3d9_timing.h").write_text(header, encoding="utf-8")
    (tree / "util/log/log.h").write_text(LOG_STUB, encoding="utf-8")
    (tree / "util/util_env.h").write_text(ENV_STUB, encoding="utf-8")
    harness = output / f"{name}-harness.cpp"
    harness.write_text(HARNESS, encoding="utf-8")
    binary = output / f"{name}-benchmark"
    command = [
        "g++", "-std=c++17", "-O2", "-g", "-Wall", "-Wextra", "-Werror", "-D__ANDROID__",
        "-ffunction-sections", "-fdata-sections", f"-I{tree / 'd3d9'}", str(harness),
        "-Wl,--gc-sections", "-o", str(binary),
    ]
    result = subprocess.run(command, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    return binary


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--before-patch", type=Path, required=True)
    parser.add_argument("--after-patch", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--calls", type=int, default=20000000)
    parser.add_argument("--rounds", type=int, default=24)
    args = parser.parse_args()
    if args.calls <= 0 or args.rounds <= 0:
        raise ValueError("calls and rounds must be positive")
    args.output.mkdir(parents=True, exist_ok=False)
    before_patch = args.before_patch.read_bytes()
    after_patch = args.after_patch.read_bytes()
    before = extract_header(args.before_patch)
    after = extract_header(args.after_patch)
    binaries = {
        "baseline": compile_variant(before, args.output, "baseline"),
        "a-a": compile_variant(before, args.output, "a-a"),
        "candidate": compile_variant(after, args.output, "candidate"),
    }
    metadata = {
        "part": "dxvk-disabled-d3d9-timing-guard",
        "machine": platform.machine(),
        "before_patch_sha256": hashlib.sha256(before_patch).hexdigest(),
        "after_patch_sha256": hashlib.sha256(after_patch).hexdigest(),
        "before_header_sha256": hashlib.sha256(before.encode()).hexdigest(),
        "after_header_sha256": hashlib.sha256(after.encode()).hexdigest(),
        "physical_android_device": False,
        "protocol": "initialized disabled guard; balanced baseline/A-A/candidate; untimed arithmetic checksum control",
        "timing": benchmark(binaries, args.output, args.calls, args.rounds),
        "interpretation": "Exact-header native guard microfixture only; not D3D9 method time, Android device FPS, GPU work, or frame pacing.",
    }
    (args.output / "result.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "part": metadata["part"], "machine": metadata["machine"],
        "candidate": metadata["timing"]["candidate_paired"], "a_a": metadata["timing"]["a-a_paired"],
        "result": str(args.output / "result.json"),
    }))


if __name__ == "__main__":
    main()
