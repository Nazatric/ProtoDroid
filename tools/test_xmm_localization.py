#!/usr/bin/env python3
"""Decode/runtime tests for selective XMM/SSE register localization."""
from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import types

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("recomp", ROOT / "tools/recomp/recomp.py")
recomp = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(recomp)

md = recomp.capstone.Cs(recomp.capstone.CS_ARCH_X86, recomp.capstone.CS_MODE_32)
md.detail = True


class SyntheticPE:
    def __init__(self, code: bytes):
        self.code = code
        self.sections = [types.SimpleNamespace(VirtualAddress=0, Misc_VirtualSize=len(code))]

    def get_data(self, offset: int, size: int) -> bytes:
        if 0 <= offset < len(self.code):
            return self.code[offset:offset + size]
        return b""


class BaselineCompiler(recomp.FunctionCompiler):
    def use_local_xmm(self, body, decoded):
        return True


def generate(hex_code: str, compiler_class=recomp.FunctionCompiler):
    code = bytes.fromhex(hex_code)
    compiler = compiler_class(md, SyntheticPE(code), recomp.BASE, len(code), {recomp.BASE})
    return compiler.compile(), compiler


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def runtime_differential() -> None:
    cases = [
        "0f28c1 c3",  # movaps xmm0, xmm1: direct full-register bit movement
        "0f58c1 c3",  # addps xmm0, xmm1: direct arithmetic on every lane
        "f30f10c1 c3",  # movss xmm0, xmm1: direct scalar/lane behavior
        "0f28c1 0f28c1 0f28c1 0f28c1 c3",  # four operations stay direct
        "0f28c1 0f28c1 0f28c1 0f28c1 0f28c1 c3",  # five operations retain locals
        "0f28c1 49 75fa c3",  # one operation in a backward loop retains locals
        "0f28c1 e8 f80f0000 0f28c1 c3",  # callee changes all XMM state
        "d9e1 0f28c1 0f28c1 0f28c1 0f28c1 0f28c1 c3",  # direct x87 + local XMM
        "d9e8 dec1 d9e8 dec1 d9e8 dec1 0f28c1 c3",  # local x87 + direct XMM
    ]
    functions = []
    for index, code in enumerate(cases):
        baseline, _ = generate(code, BaselineCompiler)
        candidate, _ = generate(code)
        functions.append(baseline.replace("f_10000000", f"baseline_{index}"))
        functions.append(candidate.replace("f_10000000", f"candidate_{index}"))
    harness = r'''
#include <stdio.h>
#include <stdlib.h>
uint8_t* RecompMemoryBase;
volatile uint32_t RecompRestrictLo = 0, RecompRestrictHi = 0, RecompTestDetours = 0;
#define MEMORY_SIZE 65536
static uint32_t rng_state = 0x9e3779b9u;
static uint32_t next_random(void) {
    rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5;
    return rng_state;
}
static void store32(uint8_t* memory, uint32_t address, uint32_t value) {
    memcpy(memory + address, &value, sizeof value);
}
void RecompDispatch(Ctx* c, uint32_t target) {
    if (target != 0x10001000u) abort();
    for (int i = 0; i < 8; ++i)
        for (int k = 0; k < 4; ++k)
            c->xmm[i].u32[k] ^= 0x9e3779b9u + (uint32_t)(i * 17 + k);
    c->st[c->st_top] += 0.5;
    c->st_top = (c->st_top + 1) & 7;
    c->st[c->st_top] -= 0.25;
    c->fpu_cw ^= 0x0300u;
    c->eax ^= 0xa5a5a5a5u;
    c->ecx ^= 0x5a5a5a5au;
    c->edx ^= 0x3c3c3c3cu;
}
typedef void (*Function)(Ctx*);
static int run_pair(int index, Function baseline, Function candidate) {
    uint8_t* a_memory = malloc(MEMORY_SIZE), * b_memory = malloc(MEMORY_SIZE);
    if (!a_memory || !b_memory) return 90;
    for (int trial = 0; trial < 256; ++trial) {
        for (int i = 0; i < MEMORY_SIZE; ++i) a_memory[i] = (uint8_t)next_random();
        store32(a_memory, 0x8000, 0x12345678u);
        Ctx a = {0};
        a.esp = 0x8000;
        a.ecx = index == 5 ? (uint32_t)(trial % 9 + 1) : next_random();
        a.edx = next_random();
        a.eax = next_random();
        a.st_top = (uint32_t)(trial & 7);
        a.fpu_cw = trial % 3 == 0 ? 0x007f : 0x027f;
        a.fpu_sw = (uint16_t)next_random();
        for (int i = 0; i < 8; ++i) {
            a.st[i] = (double)(int32_t)next_random() / (double)(1u + (next_random() & 0xffffu));
            for (int k = 0; k < 4; ++k) {
                if (index == 1)
                    a.xmm[i].f[k] = (float)(int32_t)next_random() /
                                    (float)(1u + (next_random() & 0xffffu));
                else
                    a.xmm[i].u32[k] = next_random();
            }
        }
        Ctx b = a;
        memcpy(b_memory, a_memory, MEMORY_SIZE);
        RecompMemoryBase = a_memory; baseline(&a);
        RecompMemoryBase = b_memory; candidate(&b);
        if (memcmp(&a, &b, sizeof a) || memcmp(a_memory, b_memory, MEMORY_SIZE)) {
            fprintf(stderr, "case %d trial %d differs: eax %08x/%08x esp %08x/%08x top %u/%u cw %04x/%04x\n",
                    index, trial, a.eax, b.eax, a.esp, b.esp, a.st_top, b.st_top, a.fpu_cw, b.fpu_cw);
            for (int i = 0; i < 8; ++i)
                if (memcmp(&a.xmm[i], &b.xmm[i], sizeof a.xmm[i]))
                    fprintf(stderr, "xmm%d %08x/%08x %08x/%08x %08x/%08x %08x/%08x\n", i,
                            a.xmm[i].u32[0], b.xmm[i].u32[0], a.xmm[i].u32[1], b.xmm[i].u32[1],
                            a.xmm[i].u32[2], b.xmm[i].u32[2], a.xmm[i].u32[3], b.xmm[i].u32[3]);
            return 1;
        }
    }
    free(a_memory); free(b_memory);
    return 0;
}
int main(void) {
'''
    for index in range(len(cases)):
        harness += f"    int result_{index} = run_pair({index}, baseline_{index}, candidate_{index});\n"
        harness += f"    if (result_{index}) return result_{index};\n"
    harness += '    puts("XMM runtime differential: 9 cases x 256 trials match");\n    return 0;\n}\n'
    with tempfile.TemporaryDirectory(prefix="protodroid-xmm-runtime-") as temporary:
        root = Path(temporary)
        source = root / "differential.c"
        executable = root / "differential"
        source.write_text('#include "runtime.h"\n#include "runtime_fp.h"\n' + "\n\n".join(functions) + harness)
        subprocess.run([
            os.environ.get("CC", "cc"), "-std=c11", "-O2", "-w", "-DRECOMP_TSO",
            "-fsigned-char", "-fno-strict-aliasing", "-fwrapv", "-fsanitize=undefined",
            "-fno-sanitize-recover=all", f"-I{ROOT / 'src/recomp'}", str(source), "-o", str(executable),
        ], cwd=ROOT, check=True)
        subprocess.run([str(executable)], cwd=ROOT, check=True)


def main() -> int:
    output, _ = generate("0f28c1 c3")
    require("XmmValue r_xmm" not in output and "c->xmm[0] = c->xmm[1]" in output,
            "one-instruction SSE helper did not use direct XMM state")

    output, _ = generate("0f28c1 0f28c1 0f28c1 0f28c1 c3")
    require("XmmValue r_xmm" not in output and output.count("c->xmm[0] = c->xmm[1]") == 4,
            "four-operation straight-line SSE helper was needlessly localized")

    output, _ = generate("0f28c1 0f28c1 0f28c1 0f28c1 0f28c1 c3")
    require("XmmValue r_xmm0" in output and "r_xmm0 = r_xmm1" in output,
            "longer SSE sequence lost the baseline localization path")

    output, _ = generate("0f28c1 49 75fa c3")
    require("XmmValue r_xmm0" in output and "r_xmm0 = r_xmm1" in output,
            "a backward loop did not retain XMM localization")

    output, _ = generate("0f28c1 e8 f80f0000 0f28c1 c3")
    require("XmmValue r_xmm" not in output and "RecompDispatch" in output and
            output.count("c->xmm[0] = c->xmm[1]") == 2,
            "short SSE helper with a call retained stale-prone local XMM state")

    output, _ = generate("d9e1 0f28c1 0f28c1 0f28c1 0f28c1 0f28c1 c3")
    require("double r_st[8]" not in output and "X87(c, 0)" in output and "XmmValue r_xmm0" in output,
            "independent direct-x87/local-XMM policy was not preserved")

    output, _ = generate("d9e8 dec1 d9e8 dec1 d9e8 dec1 0f28c1 c3")
    require("double r_st[8]" in output and "X87PushL" in output and "XmmValue r_xmm" not in output and
            "c->xmm[0] = c->xmm[1]" in output,
            "independent local-x87/direct-XMM policy was not preserved")

    runtime_differential()
    print("Selective XMM localization tests passed (7 decode cases, 9 runtime cases)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
