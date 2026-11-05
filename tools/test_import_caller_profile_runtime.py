#!/usr/bin/env python3
"""UBSan accumulator tests plus actual C++ formatter -> analyzer -> real symbol-table validation."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="import-caller-profile-") as temp_name:
    temp = Path(temp_name)
    binary = temp / "import_caller_profile_test"
    subprocess.run(shlex.split(os.environ.get("CXX", "g++")) + [
        "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=undefined", "-fno-sanitize-recover=all", f"-I{ROOT / 'src/native'}",
        str(ROOT / "src/native/tests/import_caller_profile_test.cpp"), "-o", str(binary),
    ], cwd=ROOT, check=True)
    env = {**os.environ, "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1"}
    subprocess.run([str(binary)], cwd=ROOT, check=True, env=env)
    golden = subprocess.run([str(binary), "--emit-golden"], cwd=ROOT, check=True, env=env,
                            text=True, capture_output=True).stdout
    log = temp / "golden.log"
    log.write_text(golden, encoding="utf-8")
    result = subprocess.run([sys.executable, str(ROOT / "tools/analyze_import_callers.py"), str(log),
                             "--functions", str(ROOT / "re/functions.csv")],
                            cwd=ROOT, check=True, text=True, capture_output=True)
    report = json.loads(result.stdout)
    method = report["methods"][0]
    assert method["total_ns"] == 100 and method["total_calls"] == 4 and method["unlisted_calls"] == 1
    assert len(method["callers"]) == 3
    assert all(caller["resolution"] == "containing-function" and
               caller["symbol"]["name"] == "engine::streampackagestates::Loading::vfunction3"
               for caller in method["callers"])
print("Import caller-profile end-to-end checks passed (UBSan, real formatter, analyzer, real functions.csv)")
