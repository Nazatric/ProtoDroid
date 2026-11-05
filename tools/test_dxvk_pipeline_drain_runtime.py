#!/usr/bin/env python3
"""Compile/run the actual DXVK pipeline-drain helper with fake clocks and UBSan."""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    raise SystemExit("usage: test_dxvk_pipeline_drain_runtime.py <patched DXVK source root>")
ROOT = Path(__file__).resolve().parents[1]
DXVK = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="protodroid-pipeline-drain-") as temp:
    binary = Path(temp) / "pipeline_drain_test"
    command = shlex.split(os.environ.get("CXX", "g++")) + [
        "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=undefined", "-fno-sanitize-recover=all",
        f"-I{DXVK / 'src/dxvk'}", str(ROOT / "tools/dxvk_pipeline_drain_test.cpp"),
        "-o", str(binary),
    ]
    subprocess.run(command, cwd=ROOT, check=True)
    subprocess.run([str(binary)], cwd=ROOT, check=True, env={**os.environ, "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1"})
print("DXVK pipeline-drain host runtime checks passed (UBSan)")
