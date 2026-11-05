#!/usr/bin/env python3
"""Measure exact RECOMP_TSO headers on aligned ordered loads with an A/A control."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess

from measure_d3d9_bridge import benchmark


HARNESS = r'''
#include <array>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#define RECOMP_TSO 1
#include "runtime.h"

uint8_t* RecompMemoryBase = nullptr;
constexpr uint32_t kAddresses = 4096;
std::array<uint32_t, kAddresses> g_addresses;

__attribute__((noinline)) uint64_t Run(uint64_t iterations, uint64_t* checksum) {
    uint64_t sum = 0;
    const auto begin = std::chrono::steady_clock::now();
    for (uint64_t i = 0; i < iterations; ++i) {
        // Values in this table are aligned at runtime, but are opaque to the compiler. The alignment branch and
        // ordered access therefore remain in the measured loop instead of being folded into a plain load.
        const uint32_t a = g_addresses[(i * 17 + (i >> 7)) & (kAddresses - 1)];
        __asm__ volatile("" : : "r"(a) : "memory");
        sum += R32(a);
        sum += R16(a + 4);
        sum += R32(a + 8);
        sum += R16(a + 12);
    }
    const auto end = std::chrono::steady_clock::now();
    *checksum = sum;
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
}

int main(int argc, char** argv) {
    const uint64_t iterations = argc > 1 ? std::strtoull(argv[1], nullptr, 0) : 5000000;
    if (!iterations) return 2;
    alignas(64) static uint8_t memory[kAddresses * 16 + 64];
    RecompMemoryBase = memory;
    for (uint32_t i = 0; i < kAddresses; ++i) {
        const uint32_t a = i * 16;
        g_addresses[i] = a;
        W32(a, 0x10203040u + a);
        W16(a + 4, uint16_t(0x5060u + a));
        W32(a + 8, 0x708090a0u + a);
        W16(a + 12, uint16_t(0xb0c0u + a));
    }
    uint64_t ignored = 0;
    Run(100000, &ignored);
    uint64_t sum = 0;
    const uint64_t elapsed = Run(iterations, &sum);
    // Recompute independently of the measured header operations to validate every load's exact value.
    uint64_t expected = 0;
    for (uint64_t i = 0; i < iterations; ++i) {
        const uint32_t a = g_addresses[(i * 17 + (i >> 7)) & (kAddresses - 1)];
        const uint32_t v0 = 0x10203040u + a;
        const uint16_t v1 = uint16_t(0x5060u + a);
        const uint32_t v2 = 0x708090a0u + a;
        const uint16_t v3 = uint16_t(0xb0c0u + a);
        expected += uint64_t(v0) + v1 + v2 + v3;
    }
    std::printf("{\"calls\":%" PRIu64 ",\"elapsed_ns\":%" PRIu64 ",\"sum\":%" PRIu64 "}\n",
                iterations, elapsed, sum);
    return sum == expected ? 0 : 6;
}
'''


def compile_variant(header: str, output: Path, name: str) -> Path:
    tree = output / name
    tree.mkdir()
    (tree / "runtime.h").write_text(header, encoding="utf-8")
    harness = output / f"{name}-harness.cpp"
    harness.write_text(HARNESS, encoding="utf-8")
    binary = output / f"{name}-benchmark"
    command = ["g++", "-std=c++17", "-O2", "-g", "-Wall", "-Wextra", "-Werror"]
    if platform.machine() == "aarch64":
        command.append("-march=armv8.2-a+rcpc")
    command += [f"-I{tree}", str(harness), "-o", str(binary)]
    result = subprocess.run(command, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    return binary


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--before-header", type=Path, required=True)
    parser.add_argument("--after-header", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--calls", type=int, default=5000000)
    parser.add_argument("--rounds", type=int, default=24)
    args = parser.parse_args()
    if args.calls <= 0 or args.rounds <= 0:
        raise ValueError("calls and rounds must be positive")
    args.output.mkdir(parents=True, exist_ok=False)
    before_bytes = args.before_header.read_bytes()
    after_bytes = args.after_header.read_bytes()
    before = before_bytes.decode("utf-8")
    after = after_bytes.decode("utf-8")
    for label, text in (("before", before), ("after", after)):
        if "#define RECOMP_ALIGNED(a, n)" not in text or "static inline uint32_t R32" not in text:
            raise ValueError(f"{label}: incomplete production runtime header")
    binaries = {
        "baseline": compile_variant(before, args.output, "baseline"),
        "a-a": compile_variant(before, args.output, "a-a"),
        "candidate": compile_variant(after, args.output, "candidate"),
    }
    metadata = {
        "part": "recomp-aligned-first-tso-layout",
        "machine": platform.machine(),
        "before_header_sha256": hashlib.sha256(before_bytes).hexdigest(),
        "after_header_sha256": hashlib.sha256(after_bytes).hexdigest(),
        "ordered_loads_per_timed_process": args.calls * 4,
        "physical_android_device": False,
        "protocol": "opaque aligned addresses; exact checksum; balanced baseline/A-A/candidate processes",
        "timing": benchmark(binaries, args.output, args.calls, args.rounds),
        "interpretation": "Exact-header native ordered-load layout microfixture only; not guest-code execution, Android FPS, GPU work, or frame pacing.",
    }
    (args.output / "result.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "part": metadata["part"], "machine": metadata["machine"],
        "candidate": metadata["timing"]["candidate_paired"], "a_a": metadata["timing"]["a-a_paired"],
        "result": str(args.output / "result.json"),
    }))


if __name__ == "__main__":
    main()
