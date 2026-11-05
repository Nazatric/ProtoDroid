#!/usr/bin/env python3
"""Compile and run the warm-cache scheduler with UBSan and warnings as errors."""
from pathlib import Path
import os
import shlex
import subprocess
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: test_dxvk_warm_cache_trace_runtime.py <patched DXVK source root>")
ROOT = Path(__file__).resolve().parents[1]
DXVK = Path(sys.argv[1]).resolve()
required = [
    DXVK / "src/d3d9/d3d9_warm_cache_trace.h",
    DXVK / "src/dxvk/dxvk_pipeline_drain.h",
    DXVK / "include/native/directx/d3d9.h",
    DXVK / "include/native/windows/windows.h",
    ROOT / "tools/dxvk_warm_cache_trace_test.cpp",
]
missing = [str(path) for path in required if not path.is_file()]
if missing:
    raise SystemExit("patched DXVK sources/header submodules are required:\n" + "\n".join(missing))
out = Path("/tmp/protodroid-dxvk-warm-cache-trace-test")
command = shlex.split(os.environ.get("CXX", "g++")) + [
    "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
    "-fsanitize=undefined", "-fno-sanitize-recover=all", "-pthread",
    "-D__ANDROID__=1", "-D_WIN32_WINNT=0x0a00", "-DNOMINMAX", "-D__declspec(x)=",
    f"-I{DXVK / 'src'}", f"-I{DXVK / 'src/d3d9'}", f"-I{DXVK / 'src/dxvk'}",
    f"-I{DXVK / 'include/native/windows'}", f"-I{DXVK / 'include/native/directx'}",
    str(ROOT / "tools/dxvk_warm_cache_trace_test.cpp"), "-o", str(out),
]
subprocess.run(command, cwd=ROOT, check=True)
env = dict(os.environ)
env["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=1"
subprocess.run([str(out)], cwd=ROOT, env=env, check=True)
print("DXVK warm-cache trace host runtime checks passed (UBSan; not device performance evidence)")
