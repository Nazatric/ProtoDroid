#!/usr/bin/env python3
"""Test and measure cross-part generated-call ThinLTO on the native host.

This is a synthetic generated-ABI mechanism benchmark, not Android device FPS. The baseline must retain a native
call to the separately compiled guest callee; ThinLTO must remove that call while preserving guest stack/context.
Timing uses balanced baseline/A-A/candidate processes and has no performance pass/fail threshold.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import itertools
import json
import os
from pathlib import Path
import platform
import re
import shlex
import statistics
import subprocess
import tempfile
import types

ROOT = Path(__file__).resolve().parents[1]
CALLER_CODE = bytes.fromhex("b8 01000000 e8 f6000000 89 c3 c3")
CALLEE_CODE = bytes.fromhex("40 c3")  # inc eax; ret


class SyntheticPE:
    def __init__(self, code: bytes, va: int, base: int):
        self.code = code
        self.va = va
        self.base = base
        self.sections = [types.SimpleNamespace(VirtualAddress=va - base, Misc_VirtualSize=len(code))]

    def get_data(self, offset: int, size: int) -> bytes:
        offset -= self.va - self.base
        return self.code[offset:offset + size] if 0 <= offset < len(self.code) else b""


def generate(module, va: int, code: bytes) -> str:
    md = module.capstone.Cs(module.capstone.CS_ARCH_X86, module.capstone.CS_MODE_32)
    md.detail = True
    pe = SyntheticPE(code, va, module.BASE)
    compiler = module.FunctionCompiler(md, pe, va, len(code), {module.BASE, module.BASE + 0x100})
    output = compiler.compile()
    if "no decodable instruction" in output or "RecompUnimplemented" in output:
        raise AssertionError("synthetic input did not decode")
    return output


def caller_body(disassembly: str) -> str:
    match = re.search(r"(?m)^\s*[0-9a-f]+ <f_10000000>:\n", disassembly)
    if not match:
        raise AssertionError("native caller symbol missing")
    rest = disassembly[match.end():]
    next_symbol = re.search(r"(?m)^\s*[0-9a-f]+ <[^>]+>:\n", rest)
    return rest[:next_symbol.start()] if next_symbol else rest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rounds", type=int, default=24)
    parser.add_argument("--iterations", type=int, default=1000000)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.rounds < 6 or args.iterations < 1:
        raise SystemExit("use at least six rounds and one iteration")
    cc = shlex.split(os.environ.get("CC", "cc"))
    objdump = shlex.split(os.environ.get("OBJDUMP", "objdump"))
    extra = shlex.split(os.environ.get("TARGET_FLAGS", ""))
    link_extra = shlex.split(os.environ.get("LINK_FLAGS", ""))

    spec = importlib.util.spec_from_file_location("recomp", ROOT / "tools/recomp/recomp.py")
    recomp = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(recomp)

    with tempfile.TemporaryDirectory(prefix="protodroid-thin-lto-") as temporary:
        work = Path(temporary)
        for name, va, code in (("caller", recomp.BASE, CALLER_CODE),
                               ("callee", recomp.BASE + 0x100, CALLEE_CODE)):
            source = '#include "runtime.h"\n#include "runtime_fp.h"\n' + generate(recomp, va, code) + "\n"
            (work / f"{name}.c").write_text(source)
        support = work / "support.c"
        support.write_text(r'''
#define _POSIX_C_SOURCE 200809L
#include "runtime.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
void f_10000000(Ctx* c);
static uint64_t guest_stack[64];
void RecompDispatch(Ctx* c, uint32_t target) { (void)c; (void)target; _Exit(90); }
static uint64_t now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) _Exit(91);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}
int main(int argc, char** argv) {
    if (argc != 2) return 64;
    char* end = NULL;
    uint64_t iterations = strtoull(argv[1], &end, 10);
    if (!end || *end || !iterations) return 66;
    if ((uintptr_t)guest_stack > UINT32_MAX) {
        fputs("non-PIE guest stack is not 32-bit addressable\n", stderr);
        return 67;
    }
    const uint32_t initial_esp = (uint32_t)(uintptr_t)&guest_stack[32];
    guest_stack[32] = 0x12345678u;
    Ctx c = {0};
    for (int i = 0; i < 10000; ++i) {
        c.esp = initial_esp; guest_stack[32] = 0x12345678u; f_10000000(&c);
    }
    uint64_t start = now_ns();
    for (uint64_t i = 0; i < iterations; ++i) {
        c.esp = initial_esp; guest_stack[32] = 0x12345678u; f_10000000(&c);
    }
    uint64_t elapsed = now_ns() - start;
    if (c.eax != 2 || c.ebx != 2 || c.esp != initial_esp + 4 || c.ret_target != 0x12345678u) {
        fprintf(stderr, "context mismatch eax=%x ebx=%x esp=%x ret=%x\n", c.eax, c.ebx, c.esp, c.ret_target);
        return 1;
    }
    printf("{\"iterations\":%" PRIu64 ",\"elapsed_ns\":%" PRIu64 "}\n", iterations, elapsed);
    return 0;
}
''')
        flags = ["-std=c11", "-O2", "-g0", "-w", "-fPIC", "-fno-strict-aliasing", "-fwrapv",
                 "-fno-delete-null-pointer-checks", "-DRECOMP_FLAT_MEMORY", "-DRECOMP_GUEST_BASE=0",
                 f"-I{ROOT / 'src/recomp'}", *extra]
        support_obj = work / "support.o"
        subprocess.run([*cc, *flags, "-c", str(support), "-o", str(support_obj)], check=True, cwd=ROOT)
        bodies = {}
        binaries = {}
        for mode in ("baseline", "candidate"):
            lto = ["-flto=thin", "-fno-semantic-interposition"] if mode == "candidate" else []
            objects = []
            for name in ("caller", "callee"):
                obj = work / f"{name}-{mode}.o"
                subprocess.run([*cc, *flags, *lto, "-c", str(work / f"{name}.c"), "-o", str(obj)],
                               check=True, cwd=ROOT)
                objects.append(obj)
            executable = work / mode
            subprocess.run([*cc, *lto, "-no-pie", "-Wl,--export-dynamic", *map(str, objects),
                            str(support_obj), "-o", str(executable)], check=True, cwd=ROOT)
            disassembly = subprocess.check_output([*objdump, "-d", str(executable)], text=True)
            body = caller_body(disassembly)
            bodies[mode] = body
            has_callee_call = re.search(r"\b(?:call|bl)\b[^\n]*<f_10000100>", body) is not None
            if mode == "baseline" and not has_callee_call:
                raise AssertionError("baseline unexpectedly inlined the separate-file callee:\n" + body)
            if mode == "candidate" and has_callee_call:
                raise AssertionError("ThinLTO retained the cross-part native callee call:\n" + body)
            subprocess.run([str(executable), "1000"], check=True, cwd=ROOT, stdout=subprocess.DEVNULL)
            binaries[mode] = executable

        variants = ("baseline", "baseline_aa", "candidate")
        orders = list(itertools.permutations(variants))
        raw = []
        for round_index in range(args.rounds):
            for order_index, variant in enumerate(orders[round_index % len(orders)]):
                mode = "baseline" if variant == "baseline_aa" else variant
                output = subprocess.check_output([str(binaries[mode]), str(args.iterations)], text=True)
                row = json.loads(output)
                row.update(round=round_index, order_index=order_index, variant=variant)
                raw.append(row)
        lookup = {(r["round"], r["variant"]): r["elapsed_ns"] for r in raw}
        paired = [{
            "round": i,
            "candidate_over_baseline": lookup[i, "candidate"] / lookup[i, "baseline"],
            "aa_control_over_baseline": lookup[i, "baseline_aa"] / lookup[i, "baseline"],
        } for i in range(args.rounds)]
        medians = {
            name: statistics.median(r["elapsed_ns"] / args.iterations for r in raw if r["variant"] == name)
            for name in variants
        }
        result = {
            "schema": 1,
            "kind": "native-cross-part-generated-thinlto-microbenchmark-not-android-device-fps",
            "physical_android_device": False,
            "platform": platform.platform(),
            "machine": platform.machine(),
            "compiler": subprocess.check_output([*cc, "--version"], text=True).splitlines()[0],
            "generator_sha256": hashlib.sha256((ROOT / "tools/recomp/recomp.py").read_bytes()).hexdigest(),
            "rounds": args.rounds,
            "iterations_per_measurement": args.iterations,
            "median_ns_per_guest_entry": medians,
            "median_paired_candidate_over_baseline": statistics.median(
                r["candidate_over_baseline"] for r in paired),
            "median_paired_aa_control_over_baseline": statistics.median(
                r["aa_control_over_baseline"] for r in paired),
            "baseline_caller_disassembly": bodies["baseline"],
            "candidate_caller_disassembly": bodies["candidate"],
            "paired_ratios": paired,
            "raw": raw,
        }
        if args.output:
            args.output.write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps({k: result[k] for k in (
            "kind", "machine", "median_ns_per_guest_entry", "median_paired_candidate_over_baseline",
            "median_paired_aa_control_over_baseline")}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
