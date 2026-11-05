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
            "-DPROTODROID_GUEST_FUNCTION_TRACE=1",
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

        # Match generated C, not only C++: ordinary objects must have no probe symbol references at all, while the
        # explicit investigation object retains exactly the two renamed runtime entry points.
        fixture = ROOT / "src/native/tests/guest_function_probe_compile_test.c"
        for name, define in (("ordinary", []), ("instrumented", ["-DPROTODROID_GUEST_FUNCTION_TRACE=1"])):
            obj = root / f"probes-{name}.o"
            subprocess.run([
                os.environ.get("CC", "cc"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                *define, f"-I{ROOT / 'src/recomp'}", "-c", str(fixture), "-o", str(obj),
            ], cwd=ROOT, env=environment, check=True)
            undefined = subprocess.check_output([os.environ.get("NM", "nm"), "-u", str(obj)], text=True)
            has_enter = "RecompGuestFunctionEnterRuntime" in undefined
            has_exit = "RecompGuestFunctionExitRuntime" in undefined
            if name == "ordinary" and (has_enter or has_exit or "RecompGuestFunction" in undefined):
                raise AssertionError(f"ordinary object retained guest probe references: {undefined}")
            if name == "instrumented" and not (has_enter and has_exit):
                raise AssertionError(f"instrumented object lost guest probe references: {undefined}")
    print("guest function trace runtime/analyzer and ordinary compile-out tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
