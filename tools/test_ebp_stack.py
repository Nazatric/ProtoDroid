#!/usr/bin/env python3
"""Decode-level positive and negative tests for proven EBP frame stack accesses."""
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
    def relax_ebp_stack(self, ins, line):
        return line


def generate(hex_code: str, compiler_class=recomp.FunctionCompiler):
    code = bytes.fromhex(hex_code)
    compiler = compiler_class(md, SyntheticPE(code), recomp.BASE, len(code), {recomp.BASE})
    output = compiler.compile()
    return output, compiler


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def ordered_ebp(output: str) -> bool:
    return "R32((uint32_t)(r_ebp" in output and "SR32((uint32_t)(r_ebp" not in output


def runtime_differential() -> None:
    cases = [
        # Integer argument and two local rounds.
        "55 8b ec 83 ec 10 8b 45 08 01 45 fc 8b 4d fc 89 4d f8 8b 45 f8 8b e5 5d c3",
        # A frame fact must survive this loop's backedge.
        "55 8b ec 83 ec 10 8b 4d 08 c7 45 fc 00 00 00 00 01 4d fc 49 75 fa "
        "8b 45 fc 8b e5 5d c3",
        # Floating-point loads/stores use the same proven stack contract.
        "55 8b ec 83 ec 10 f3 0f 10 45 08 f3 0f 11 45 fc f3 0f 10 4d fc "
        "f3 0f 11 4d f8 8b 45 f8 8b e5 5d c3",
        # After pop ebp, [ebp+8] is caller-selected memory, not the current frame.
        "55 8b ec 8b e5 5d 8b 45 08 c3",
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
#define MEMORY_SIZE 65536
static uint32_t rng_state = 0x9e3779b9u;
static uint32_t next_random(void) {
    rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5;
    return rng_state;
}
static void store32(uint8_t* memory, uint32_t address, uint32_t value) {
    memcpy(memory + address, &value, sizeof value);
}
typedef void (*Function)(Ctx*);
static int run_case(int index, Function baseline, Function candidate) {
    uint8_t* a_memory = malloc(MEMORY_SIZE), *b_memory = malloc(MEMORY_SIZE);
    if (!a_memory || !b_memory) return 90;
    for (int trial = 0; trial < 128; ++trial) {
        for (int i = 0; i < MEMORY_SIZE; ++i) a_memory[i] = (uint8_t)next_random();
        memcpy(b_memory, a_memory, MEMORY_SIZE);
        uint32_t argument = index == 1 ? (uint32_t)(trial % 8 + 1) : next_random();
        store32(a_memory, 0x8000, 0xdead0000u + (uint32_t)index);
        store32(a_memory, 0x8004, argument);
        store32(a_memory, 0x9008, next_random());
        memcpy(b_memory, a_memory, MEMORY_SIZE);
        Ctx a = {0}, b = {0};
        a.esp = b.esp = 0x8000; a.ebp = b.ebp = 0x9000;
        a.ebx = b.ebx = 0x9100; a.ecx = b.ecx = (uint32_t)trial;
        a.fpu_cw = b.fpu_cw = 0x027f;
        RecompMemoryBase = a_memory; baseline(&a);
        RecompMemoryBase = b_memory; candidate(&b);
        if (memcmp(&a, &b, sizeof a) || memcmp(a_memory, b_memory, MEMORY_SIZE)) {
            fprintf(stderr, "case %d trial %d differs: eax %08x/%08x esp %08x/%08x ebp %08x/%08x\n",
                    index, trial, a.eax, b.eax, a.esp, b.esp, a.ebp, b.ebp);
            return 1;
        }
    }
    free(a_memory); free(b_memory);
    return 0;
}
int main(void) {
'''
    for index in range(len(cases)):
        harness += f"    int result_{index} = run_case({index}, baseline_{index}, candidate_{index});\n"
        harness += f"    if (result_{index}) return result_{index};\n"
    harness += '    puts("EBP runtime differential: 4 cases x 128 trials match");\n    return 0;\n}\n'
    with tempfile.TemporaryDirectory(prefix="protodroid-ebp-runtime-") as temporary:
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
    output, compiler = generate("55 8b ec 8b 45 08 89 45 fc 8b e5 5d c3")
    require("SR32((uint32_t)(r_ebp + 0x8u))" in output and
            "SW32((uint32_t)(r_ebp + 0xfffffffcu)" in output and compiler.ebp_stack_sites == 2,
            "ordinary frame argument/local were not both converted")

    output, compiler = generate("8b 45 08 c3")
    require(ordered_ebp(output) and compiler.ebp_stack_sites == 0,
            "general-register EBP was treated as a frame")

    output, compiler = generate("55 8b ec 8b 6d 08 8b 45 04 8b e5 5d c3")
    require(output.count("R32((uint32_t)(r_ebp") == 2 and
            "SR32((uint32_t)(r_ebp" not in output and compiler.ebp_stack_sites == 0,
            "an EBP-writing instruction or its successors retained the frame fact")

    output, compiler = generate("55 8b ec 66 8b e8 8b 45 08 c3")
    require(ordered_ebp(output) and compiler.ebp_stack_sites == 0,
            "a 16-bit BP write failed to invalidate EBP")

    # One branch replaces EBP; the join must remain ordered even though the other path retains the frame.
    output, compiler = generate("55 8b ec 85 c9 74 02 8b eb 8b 45 08 c3")
    require(ordered_ebp(output) and compiler.ebp_stack_sites == 0,
            "a branch join used OR rather than must/AND frame facts")

    # The loop backedge must not erase a frame established before the loop.
    output, compiler = generate("55 8b ec 8b 4d 08 85 c9 74 06 ff 45 fc 49 75 fa 8b 45 fc 8b e5 5d c3")
    require(output.count("SR32((uint32_t)(r_ebp") == 3 and
            "SW32((uint32_t)(r_ebp + 0xfffffffcu)" in output and compiler.ebp_stack_sites == 4 and
            "dec ebp" not in output and "cld" not in output,
            "a proven frame did not survive the intended loop backedge and its exit")

    output, compiler = generate("55 8b ec 8b 44 8d fc 8b e5 5d c3")
    require(ordered_ebp(output) and compiler.ebp_stack_sites == 0,
            "indexed EBP addressing was assumed to be stack storage")

    output, compiler = generate("55 8b ec 64 8b 45 00 8b e5 5d c3")
    require("R32((uint32_t)(c->fs_base + r_ebp))" in output and
            "SR32" not in output and compiler.ebp_stack_sites == 0,
            "FS-relative EBP addressing was treated as an ordinary frame access")

    output, compiler = generate("55 8b ec f3 0f 10 45 fc f3 0f 11 45 f8 8b e5 5d c3")
    require("SRF32((uint32_t)(r_ebp + 0xfffffffcu))" in output and
            "SWF32((uint32_t)(r_ebp + 0xfffffff8u)" in output and compiler.ebp_stack_sites == 2,
            "floating-point frame accesses were not covered")

    output, compiler = generate("55 8b ec 01 45 fc 8b e5 5d c3")
    require("SR32((uint32_t)(r_ebp + 0xfffffffcu))" in output and
            "SW32((uint32_t)(r_ebp + 0xfffffffcu)" in output and compiler.ebp_stack_sites == 2,
            "read-modify-write frame access was not covered")

    output, compiler = generate("55 8b ec 8b e5 5d 8b 45 08 c3")
    require(ordered_ebp(output) and compiler.ebp_stack_sites == 0,
            "an access after pop ebp retained the old frame fact")

    # Ordinary ABI-conforming calls preserve EBP. Target 0x10001000 is outside this synthetic extent.
    output, compiler = generate("55 8b ec e8 f8 0f 00 00 8b 45 08 8b e5 5d c3")
    require("SR32((uint32_t)(r_ebp + 0x8u))" in output and compiler.ebp_stack_sites == 1,
            "an ordinary call incorrectly invalidated the frame")

    # The same shape calling the known setjmp target is excluded because it can return a second time.
    output, compiler = generate("55 8b ec e8 28 8a a8 00 8b 45 08 8b e5 5d c3")
    require(ordered_ebp(output) and compiler.ebp_stack_sites == 0,
            "setjmp-containing function was not conservatively excluded")

    runtime_differential()
    print("EBP frame stack tests passed (12 positive/negative cases)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
