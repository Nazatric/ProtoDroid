#!/usr/bin/env python3
"""Compile the real trace runtime, verify deferred nested logging, and analyze its actual records."""
from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("analyze_guest_function_trace", ROOT / "tools/analyze_guest_function_trace.py")
ANALYZER = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(ANALYZER)


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="protodroid-guest-trace-runtime-") as temporary:
        root = Path(temporary)
        binary = root / "trace-test"
        log = root / "trace.log"
        environment = dict(os.environ)
        environment["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=1"
        subprocess.run([
            os.environ.get("CXX", "g++"), "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=undefined", "-fno-sanitize-recover=all", "-DRECOMP_GUEST_BASE=0",
            f"-I{ROOT / 'src/native'}", f"-I{ROOT / 'src/recomp'}",
            str(ROOT / "src/native/guest_function_trace.cpp"),
            str(ROOT / "src/native/tests/guest_function_trace_runtime_test.cpp"), "-o", str(binary),
        ], cwd=ROOT, env=environment, check=True)
        disabled = subprocess.run([str(binary), "disabled"], cwd=ROOT, env=environment,
                                  text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
        if disabled.stdout:
            raise AssertionError("disabled trace emitted output")
        with log.open("w") as output:
            subprocess.run([str(binary)], cwd=ROOT, env=environment, stdout=output, check=True)
        report = ANALYZER.analyze([log], ROOT / "re/functions.csv")
        if report["status"] != "valid-guest-function-records" or report["complete_count"] != 2:
            raise AssertionError("actual runtime records did not produce two clean completions")
        requests = report["game_state_request_wrapper_arguments"]
        if len(requests) != 1 or requests[0]["arg0"] != "00000002" or requests[0]["arg2"] != "55667788":
            raise AssertionError("actual runtime request metadata differs")
        functions = {row["rva"]: row for row in report["functions"]}
        if functions["004e5250"]["symbol"] != "proto::InGameState::vfunction1_for_GameState" or \
                functions["00838b40"]["symbol"] != "FUN_10838b40":
            raise AssertionError("actual runtime symbolization differs")
    print("guest function trace runtime-to-analyzer test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
