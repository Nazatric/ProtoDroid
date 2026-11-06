#!/usr/bin/env python3
"""Decode/runtime tests for XMM entry-copy elision (local registers keep register residency)."""
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
    def xmm_entry_copies(self, decoded):
        return frozenset()


def generate(hex_code: str, compiler_class=recomp.FunctionCompiler):
    code = bytes.fromhex(hex_code)
    compiler = compiler_class(md, SyntheticPE(code), recomp.BASE, len(code), {recomp.BASE})
    return compiler.compile(), compiler


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def runtime_differential() -> None:
    cases = [
        "0f2805 00000000 0f58c1 c3",  # full 128-bit load: entry copy is dead, later add uses the local
        "0f28c1 c3",  # movaps xmm0, xmm1: destination is overwritten, source keeps its copy
        "f30f100500000000 c3",  # movss from memory preserves upper lanes: entry copy stays
        "0f580500000000 c3",  # addps reads its destination: entry copy stays
        "0f2805 00000000 0f5805 00000000 c3",  # dead write then read-modify-write
        "0f58c1 49 75fa c3",  # a backward loop that reads first keeps every copy
        "0f28c1 e8 f80f0000 0f28c1 c3",  # callee redefines all XMM state
        "d9e1 0f2805 00000000 0f58c1 c3",  # local x87 with an elided XMM copy
        "d9e8 dec1 d9e8 dec1 d9e8 dec1 0f2805 00000000 0f58c1 c3",  # local x87 + dead XMM entry
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
    output, _ = generate("0f2805 00000000 0f58c1 c3")
    require("XmmValue r_xmm0;" in output and "XmmValue r_xmm0 = c->xmm[0];" not in output,
            "a from-scratch 128-bit load still copied the dead incoming value")
    require("r_xmm0 = RecompLoad128" in output and "XmmValue r_xmm1 = c->xmm[1];" in output,
            "eliding a dead copy changed the live register's copy or the load itself")

    output, _ = generate("0f28c1 c3")
    require("XmmValue r_xmm0;" in output and "XmmValue r_xmm1 = c->xmm[1];" in output,
            "movaps did not treat the destination as overwritten but the source as live")
    require("r_xmm0 = r_xmm1" in output, "register-to-register move stopped using local registers")

    output, _ = generate("f30f100500000000 c3")
    require("XmmValue r_xmm0 = c->xmm[0];" in output,
            "a partial movss load lost the entry copy its upper lanes need")

    output, _ = generate("0f580500000000 c3")
    require("XmmValue r_xmm0 = c->xmm[0];" in output,
            "a read-modify-write addps lost its entry copy")

    output, _ = generate("0f2805 00000000 0f5805 00000000 c3")
    require("XmmValue r_xmm0;" in output and output.count("r_xmm0") >= 3,
            "a dead write followed by a read-modify-write lost the local register")

    output, _ = generate("0f58c1 49 75fa c3")
    require("XmmValue r_xmm0 = c->xmm[0];" in output and "XmmValue r_xmm1 = c->xmm[1];" in output,
            "a loop that reads before writing elided a live entry copy")

    output, _ = generate("0f58c1 e8 f80f0000 0f28c1 c3")
    require("RecompDispatch" in output and "XmmValue r_xmm0 = c->xmm[0];" in output and
            "XmmValue r_xmm1 = c->xmm[1];" in output,
            "a read before a wrapped call elided a live entry copy")

    output, _ = generate("0f2805 00000000 e8 f80f0000 0f58c1 c3")
    require("RecompDispatch" in output and "XmmValue r_xmm0;" in output and
            "c->xmm[0] = r_xmm0" in output and "r_xmm0 = c->xmm[0]" in output,
            "a dead copy before a wrapped call did not stay elided, or lost its spill/reload")

    output, _ = generate("d9e8 dec1 d9e8 dec1 d9e8 dec1 0f2805 00000000 0f58c1 c3")
    require("double r_st[8]" in output and "X87PushL" in output and "XmmValue r_xmm0;" in output,
            "local x87 and elided XMM entry copies did not coexist")

    runtime_differential()
    print("XMM entry-copy elision tests passed (7 decode cases, 9 runtime cases)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
