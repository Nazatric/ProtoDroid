"""Instruction-flag metadata that the recompiler's liveness analysis depends on.

Why this exists: v1.1 lets each flag write be skipped when the flag is not read before its next write, which is only
sound if the "writes" set is right. Capstone reports the x87 condition codes (C0-C3) as the integer flags, so `fstp`
claims to write CF and `fxch` claims to write OF. Liveness then kills a flag that the following `lahf`, `adc`, `setcc`
or `jcc` still reads, the write is folded away, and the branch uses a value from somewhere else. The engine's
ActionScript comparison path is exactly `fucompi st(1); fstp st(0); lahf; test ah,0x44; jp`, so this class of mistake
is not academic.
"""
import sys

import capstone
import capstone.x86_const as X

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "recomp"))

import recomp  # noqa: E402  (imported after the path fix)

_STATUS = 0b111111


def decoded(code, address=0x1000):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    out = list(md.disasm(code, address))
    assert out, f"nothing decoded from {code.hex()}"
    return out


def expects(code, defs, uses, what):
    """Defs must match exactly; expected uses must be present (an over-reported read is harmless)."""
    ins = decoded(code)[0]
    got_defs, got_uses = recomp.flag_sets(ins)
    assert got_defs == defs, (
        f"{what}: {ins.mnemonic} {ins.op_str} reports defs={got_defs:#04x}, expected {defs:#04x}")
    assert got_uses & uses == uses, (
        f"{what}: {ins.mnemonic} {ins.op_str} reports uses={got_uses:#04x}, missing {uses & ~got_uses:#04x}")
    return ins


# x87 stack operations: capstone's eflags metadata is the x87 condition codes, not eflags. None of these may claim to
# write an integer flag, or the next read of that flag loses the defining instruction before it. Claiming to *read* one
# is harmless (the flag simply stays live), so only the write set is asserted here.
for code, what in ((b"\xdd\xd8", "fstp st(0)"), (b"\xd9\xc9", "fxch st(1)"), (b"\xd9\xc0", "fld st(0)"),
                   (b"\xdd\x5c\x24\x04", "fstp qword [esp+4]"), (b"\xd9\xe8", "fld1"),
                   (b"\xd9\xca", "fxch st(2)"), (b"\xdd\xd9", "fstp st(1)")):
    ins = decoded(code)[0]
    defs, _uses = recomp.flag_sets(ins)
    assert defs == 0, f"{what}: {ins.mnemonic} {ins.op_str} claims to write flags {defs:#04x}, which would let the "\
                       "liveness analysis drop a real definition before a lahf/adc/jcc"

# ... but the compare that really does write them keeps it, with the mask the caller asks for.
expects(b"\xdf\xe9", _STATUS, 0, "fucompi st(1)")
expects(b"\xdb\xe9", _STATUS, 0, "fucomi st(1)")
expects(b"\x9f", 0, _STATUS, "lahf")

# Integer ALU architecture, which capstone under-reports for some encodings.
CF, ZF, SF, OF, PF, AF = 1, 2, 4, 8, 16, 32
expects(b"\x40", _STATUS & ~CF, 0, "inc eax")
expects(b"\x48", _STATUS & ~CF, 0, "dec eax")
expects(b"\x01\xd8", _STATUS, 0, "add eax, ebx")
expects(b"\x83\xe8\x01", _STATUS, 0, "sub eax, 1")
expects(b"\x85\xc0", _STATUS, 0, "test eax, eax")
expects(b"\x74\x00", 0, ZF, "je rel8")
expects(b"\x7a\x00", 0, PF, "jp rel8")
expects(b"\x11\xd8", _STATUS, CF, "adc eax, ebx")
expects(b"\x19\xd8", _STATUS, CF, "sbb eax, ebx")
expects(b"\x9e", _STATUS, 0, "sahf")
expects(b"\x9d", recomp.ALL_FLAGS, 0, "popfd")
expects(b"\x9c", 0, recomp.ALL_FLAGS, "pushfd")

# The comparison the engine's script VM runs through `lahf`: ZF/PF/CF are all live at the compare because the lahf
# reads them, and every other flag write in the sequence must leave CF alone.
fstp = decoded(b"\xdd\xd8")[0]
assert not (recomp.flag_sets(fstp)[0] & CF), "an x87 pop must not look like it writes CF"

print("recomp flag metadata checks passed: x87 stack instructions claim no integer flags, compares and lahf keep "
      "theirs, and the integer ALU table matches the architecture")
