#!/usr/bin/env python3
"""Measure exact-source native import dispatch with balanced baseline/A-A/candidate runs.

This is a native host microfixture, including on the ARM64 CI runner. It is not Android-device FPS evidence.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess

from measure_d3d9_bridge import benchmark

ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include "guest.h"
#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifndef BENCHMARK_WORKING_SET
#define BENCHMARK_WORKING_SET 1024
#endif

uint8_t* RecompMemoryBase = nullptr;
namespace native {
#ifdef BENCHMARK_GNU_TLS
__thread Ctx* tl_ctx;
#else
thread_local Ctx* tl_ctx = nullptr;
#endif
uint32_t g_engine_lo = 0;
uint32_t g_engine_hi = 0;
void Log(const char*, ...) {}
[[noreturn]] void Fatal(const char* fmt, ...) {
    std::fprintf(stderr, "import benchmark fatal: %s\n", fmt);
    std::abort();
}
uint32_t PageAlloc(uint32_t size, uint32_t at) {
    if (at) return at;
    if (size != 0x00100000u) Fatal("unexpected thunk allocation");
    return 0x1000u;
}
int SettingInt(const char*, int fallback) { return fallback; }
}

namespace {
constexpr size_t kWorkingSet = BENCHMARK_WORKING_SET;
static_assert(kWorkingSet > 0 && kWorkingSet <= 1024, "invalid benchmark working set");
std::vector<uint8_t> memory(2u << 20);
uint64_t checksum = 0;

template <uint64_t Tag>
void TaggedImport(Ctx*) { checksum += Tag; }

uint64_t ExpectedSum(uint64_t calls) {
    uint64_t expected = 0;
    for (uint64_t i = 0; i < calls; ++i) expected += (i % kWorkingSet) % 8 + 1;
    return expected;
}
}

int main(int argc, char** argv) {
    const uint64_t calls = argc > 1 ? std::strtoull(argv[1], nullptr, 0) : 2000000;
    if (!calls) return 2;
    RecompMemoryBase = memory.data();
    const native::ImportFn handlers[] = {
        TaggedImport<1>, TaggedImport<2>, TaggedImport<3>, TaggedImport<4>,
        TaggedImport<5>, TaggedImport<6>, TaggedImport<7>, TaggedImport<8>,
    };
    std::vector<std::string> names;
    names.reserve(kWorkingSet);
    for (size_t i = 0; i < kWorkingSet; ++i) names.push_back("Call" + std::to_string(i));
    std::vector<native::ImportDef> defs;
    defs.reserve(kWorkingSet);
    for (size_t i = 0; i < kWorkingSet; ++i)
        defs.push_back({"benchmark.dll", names[i].c_str(), handlers[i % 8]});
    native::RegisterImports(defs.data(), defs.size());
    std::vector<uint32_t> targets;
    targets.reserve(kWorkingSet);
    for (const auto& name : names) targets.push_back(native::ImportThunk("benchmark.dll", name.c_str()));

    Ctx ctx = {};
    Ctx outer = {};
    native::tl_ctx = &outer;
    for (uint64_t i = 0; i < 100000; ++i) {
        if (!native::DispatchImport(&ctx, targets[i % kWorkingSet])) return 3;
    }
    if (native::tl_ctx != &outer) return 4;
    checksum = 0;
    const uint64_t expected = ExpectedSum(calls);
    const auto start = std::chrono::steady_clock::now();
    for (uint64_t i = 0; i < calls; ++i) {
        if (!native::DispatchImport(&ctx, targets[i % kWorkingSet])) return 5;
    }
    const auto end = std::chrono::steady_clock::now();
    const int64_t elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    std::printf("{\"calls\":%" PRIu64 ",\"elapsed_ns\":%lld,\"sum\":%" PRIu64 "}\n",
                calls, static_cast<long long>(elapsed), checksum);
    return checksum == expected && native::tl_ctx == &outer ? 0 : 6;
}
'''


def run(command):
    return subprocess.run(command, text=True, capture_output=True)


def compile_variant(source: Path, output: Path, name: str, working_set: int) -> Path:
    text = source.read_text(encoding="utf-8")
    harness = output / f"{name}-harness.cpp"
    harness.write_text(HARNESS, encoding="utf-8")
    binary = output / name
    flags = [
        "g++", "-std=c++17", "-O2", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
        "-ffunction-sections", "-fdata-sections", "-DRECOMP_TSO", "-DRECOMP_GUEST_BASE=0ull",
        f"-DBENCHMARK_WORKING_SET={working_set}", f"-I{ROOT / 'src/native'}",
    ]
    if "extern __thread Ctx* tl_ctx;" in text:
        flags.append("-DBENCHMARK_GNU_TLS=1")
    result = run([*flags, str(source), str(harness), "-pthread", "-Wl,--gc-sections", "-o", str(binary)])
    if result.returncode:
        raise RuntimeError(result.stderr)
    return binary


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, default=ROOT / "src/native/imports.cpp")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--calls", type=int, default=2000000)
    parser.add_argument("--rounds", type=int, default=24)
    parser.add_argument("--working-set", type=int, default=1024)
    args = parser.parse_args()
    if args.calls <= 0 or args.rounds <= 0 or not 1 <= args.working_set <= 1024:
        raise ValueError("calls/rounds must be positive and working-set must be in 1..1024")
    args.output.mkdir(parents=True, exist_ok=False)
    before_bytes = args.before.read_bytes()
    after_bytes = args.after.read_bytes()
    binaries = {
        "baseline": compile_variant(args.before, args.output, "baseline", args.working_set),
        "a-a": compile_variant(args.before, args.output, "a-a", args.working_set),
        "candidate": compile_variant(args.after, args.output, "candidate", args.working_set),
    }
    metadata = {
        "part": "import-dispatch",
        "machine": platform.machine(),
        "working_set": args.working_set,
        "before_sha256": hashlib.sha256(before_bytes).hexdigest(),
        "after_sha256": hashlib.sha256(after_bytes).hexdigest(),
        "physical_android_device": False,
        "protocol": "initialized steady state; balanced baseline/A-A/candidate; tagged handler/context checksums",
        "timing": benchmark(binaries, args.output, args.calls, args.rounds),
        "interpretation": "Native host import-dispatch microfixture only; not physical Android FPS, D3D9 method time, or GPU evidence.",
    }
    (args.output / "result.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "part": "import-dispatch", "machine": metadata["machine"], "working_set": args.working_set,
        "candidate": metadata["timing"]["candidate_paired"], "a_a": metadata["timing"]["a-a_paired"],
        "result": str(args.output / "result.json"),
    }))


if __name__ == "__main__":
    main()
