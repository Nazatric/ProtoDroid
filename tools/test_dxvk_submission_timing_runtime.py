#!/usr/bin/env python3
"""Compile and execute the patched DXVK timing header with host stubs."""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    raise SystemExit("usage: test_dxvk_submission_timing_runtime.py <patched DXVK source root>")
ROOT = Path(__file__).resolve().parents[1]
DXVK = Path(sys.argv[1]).resolve()
SOURCE = ROOT / "tools/dxvk_d3d9_timing_test.cpp"
CXX = shlex.split(os.environ.get("CXX", "g++"))

with tempfile.TemporaryDirectory(prefix="protodroid-d3d9-timing-") as temp:
    temp_path = Path(temp)
    for name, extra in (("enabled", []), ("disabled", ["-DTEST_DISABLED=1"])):
        binary = temp_path / name
        command = CXX + [
            "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=undefined", "-fno-sanitize-recover=all", "-pthread",
            "-D__ANDROID__=1", "-D_WIN32_WINNT=0x0a00", "-DNOMINMAX", "-D__declspec(x)=", *extra,
            f"-I{DXVK / 'include/native/windows'}",
            f"-I{DXVK / 'src/d3d9'}", f"-I{DXVK / 'src'}",
            str(SOURCE), "-o", str(binary),
        ]
        subprocess.run(command, cwd=ROOT, check=True)
        result = subprocess.run([str(binary)], cwd=ROOT, check=True, text=True, capture_output=True)
        print(result.stdout, end="")
print("DXVK D3D9 timing host runtime checks passed (enabled and disabled, UBSan)")
