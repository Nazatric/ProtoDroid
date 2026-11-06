#!/usr/bin/env python3
"""Strict host syntax checks for both Android and non-Android DXVK branches.

This checks C++ integration with the pinned headers; it is not an Android/Bionic build
or physical-device rendering/FPS validation.
"""
from pathlib import Path
import os
import shlex
import subprocess
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: test_dxvk_submission_timing_syntax.py <patched DXVK source root>")
ROOT = Path(__file__).resolve().parents[1]
DXVK = Path(sys.argv[1]).resolve()
required = [
    DXVK / "include/native/directx/d3d9.h",
    DXVK / "include/native/windows/windows.h",
    DXVK / "include/vulkan/include/vulkan/vulkan.h",
    DXVK / "include/spirv/include/spirv/unified1/spirv.h",
]
missing = [str(path) for path in required if not path.is_file()]
if missing:
    raise SystemExit("pinned DXVK header submodules are required:\n" + "\n".join(missing))

common = shlex.split(os.environ.get("CXX", "g++")) + [
    "-std=c++17", "-fsyntax-only", "-Wall", "-Wextra", "-Werror",
    "-Wno-unused-parameter", "-Wno-missing-field-initializers",
    "-Wno-cast-function-type", "-Wno-missing-braces",
    "-D_WIN32_WINNT=0x0a00", "-DNOMINMAX", "-D__declspec(x)=",
    f"-I{DXVK / 'include/native/directx'}",
    f"-I{DXVK / 'include/native/windows'}",
    f"-I{DXVK / 'include/vulkan/include'}",
    f"-I{DXVK / 'include/spirv/include'}",
    f"-I{DXVK / 'src'}",
]
for android in (False, True):
    for filename in ("d3d9_device.cpp", "d3d9_query.cpp"):
        command = common + (["-D__ANDROID__=1"] if android else []) + [str(DXVK / "src/d3d9" / filename)]
        subprocess.run(command, cwd=ROOT, check=True)
        print(f"strict syntax {'Android' if android else 'non-Android'}: {filename} PASS")
print("DXVK submission timing syntax checks passed (4 translation-unit variants)")
