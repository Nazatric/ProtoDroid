#!/usr/bin/env python3
"""Generated-only ThinLTO Make policy and opt-out checks."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MAKE = ROOT / "src/native/Makefile"
PRINT = """print-policy:
\t@printf 'RECOMP_LTO=%s\\nRECOMP_CFLAGS=%s\\nCXXFLAGS=%s\\nLDFLAGS=%s\\n' \
'$(RECOMP_LTO)' '$(RECOMP_CFLAGS)' '$(CXXFLAGS)' '$(LDFLAGS)'
"""


def policy(*args: str) -> str:
    result = subprocess.run(["make", "-s", "-C", "src/native", *args, "--eval", PRINT, "print-policy"],
                            cwd=ROOT, check=True, text=True, stdout=subprocess.PIPE)
    return result.stdout


android = policy("PLATFORM=android")
assert "RECOMP_LTO=thin\n" in android
recomp = android.split("RECOMP_CFLAGS=", 1)[1].splitlines()[0]
cxx = android.split("CXXFLAGS=", 1)[1].splitlines()[0]
ld = android.split("LDFLAGS=", 1)[1].splitlines()[0]
assert "-flto=thin" in recomp and "-fno-semantic-interposition" in recomp
assert "-flto=thin" in ld and "-Wl,--thinlto-jobs=1" in ld
assert "-flto" not in cxx, "native C++ was unintentionally included in the generated-code experiment"
assert "-Bsymbolic" in ld and "--export-dynamic" in ld

for args in (("PLATFORM=android", "RECOMP_LTO=0"), ("PLATFORM=linux",)):
    disabled = policy(*args)
    assert "RECOMP_LTO=0\n" in disabled
    assert "-flto" not in disabled and "-fno-semantic-interposition" not in disabled

for args in (("PLATFORM=android", "RECOMP_LTO=full"), ("PLATFORM=linux", "RECOMP_LTO=thin")):
    result = subprocess.run(["make", "-s", "-C", "src/native", *args, "--eval", PRINT, "print-policy"],
                            cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    assert result.returncode != 0, f"invalid policy accepted: {args}"
print("Generated-only ThinLTO default, local-binding, symbol-export, opt-out, and invalid-policy checks passed")
