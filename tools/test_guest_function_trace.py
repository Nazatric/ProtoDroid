#!/usr/bin/env python3
"""Structural and optional real-DLL differential tests for selected guest function tracing."""
from __future__ import annotations

import argparse
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def load_verify():
    spec = importlib.util.spec_from_file_location("verify_guest_trace", ROOT / "tools/recomp/verify_guest_trace.py")
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def structural_tests() -> None:
    verify = load_verify()
    header = (ROOT / "src/native/guest_function_trace.h").read_text()
    runtime = (ROOT / "src/native/guest_function_trace.cpp").read_text()
    makefile = (ROOT / "src/native/Makefile").read_text()
    runtime_h = (ROOT / "src/recomp/runtime.h").read_text()
    require("std::vector" not in header and "std::mutex" not in header and "Log(" not in header,
            "per-thread collector acquired a dynamic/logging hot-path dependency")
    require("kMaxActive = 16" in header and "kMaxEvents = 64" in header,
            "collector bounds are missing")
    require('SettingInt("guest_function_trace", 0)' in runtime, "runtime opt-in setting is missing")
    require("kMaxLoggedRecords = 4096" in runtime and "event=cap" in runtime,
            "persistent log cap/truncation evidence is missing")
    require("if (trace.quiescent()) Flush(&trace);" in runtime,
            "nested records can be flushed inside a measured parent")
    require("GuestFunctionTrace& ThreadTrace()" in runtime and
            runtime.count("GuestFunctionTrace& trace = ThreadTrace();") == 2 and
            "thread_local GuestFunctionTrace trace;" in runtime.split("GuestFunctionTrace& ThreadTrace()", 1)[1].split(
                "bool ReserveRecord", 1)[0],
            "enter and exit do not share exactly one per-thread collector")
    enter_body = runtime.split("RecompGuestFunctionEnterRuntime(Ctx* context", 1)[1].split(
        "RecompGuestFunctionExitRuntime(Ctx* context", 1)[0]
    require("Flush(" not in enter_body and "Log(" not in enter_body,
            "entry performs logging inside the measured region")
    require("guest_function_trace.cpp" in makefile, "native runtime is not linked")
    require("GUEST_FUNCTION_TRACE ?= 0" in makefile and
            "COMMON_FLAGS += -DPROTODROID_GUEST_FUNCTION_TRACE=1" in makefile,
            "the explicit whole-target investigation build option is missing")
    require("void RecompGuestFunctionEnterRuntime(Ctx* context, uint32_t rva);" in runtime_h and
            "void RecompGuestFunctionExitRuntime(Ctx* context, uint32_t rva);" in runtime_h,
            "generated-code runtime hook declarations are missing")
    require('#ifdef PROTODROID_GUEST_FUNCTION_TRACE' in runtime_h and
            '#define RecompGuestFunctionEnter(context, rva) RecompGuestFunctionEnterRuntime((context), (rva))' in runtime_h and
            '#define RecompGuestFunctionExit(context, rva) RecompGuestFunctionExitRuntime((context), (rva))' in runtime_h and
            runtime_h.count('#define RecompGuestFunctionEnter(context, rva) ((void)0)') == 1 and
            runtime_h.count('#define RecompGuestFunctionExit(context, rva) ((void)0)') == 1,
            "ordinary generated probes are not compiled out or the enabled mapping is incomplete")

    with tempfile.TemporaryDirectory(prefix="protodroid-guest-trace-structural-") as temporary:
        root = Path(temporary)
        selection = root / "selection.txt"
        selection.write_text("10000010\n")
        valid = root / "valid.c"
        valid.write_text(
            "void f_7c000010(Ctx* c) {\n"
            "    RecompGuestFunctionEnter(c, 0x00000010u);\n"
            "    { RecompGuestFunctionExit(c, 0x00000010u); return; }\n"
            "}\n"
        )
        require(verify.verify(valid, selection)["exits"] == 1, "valid synthetic probe was rejected")

        duplicate = root / "duplicate.txt"
        duplicate.write_text("10000010\n00000010\n")
        try:
            verify.verify(valid, duplicate)
            raise AssertionError("duplicate selection was accepted")
        except ValueError as error:
            require("duplicate" in str(error), "duplicate selection failed for the wrong reason")

        missing_exit = root / "missing-exit.c"
        missing_exit.write_text(valid.read_text().replace(
            "RecompGuestFunctionExit(c, 0x00000010u); return;", "return;"))
        try:
            verify.verify(missing_exit, selection)
            raise AssertionError("untraced return was accepted")
        except ValueError as error:
            require("returns but 0 traced exits" in str(error), "missing exit failed for the wrong reason")

        wrong_rva = root / "wrong-rva.c"
        wrong_rva.write_text(valid.read_text().replace(
            "RecompGuestFunctionExit(c, 0x00000010u);", "RecompGuestFunctionExit(c, 0x00000011u);"))
        try:
            verify.verify(wrong_rva, selection)
            raise AssertionError("wrong exit RVA was accepted")
        except ValueError as error:
            require("wrong RVA" in str(error), "wrong exit RVA failed for the wrong reason")

        missing_entry = root / "missing-entry.c"
        missing_entry.write_text(valid.read_text().replace(
            "    RecompGuestFunctionEnter(c, 0x00000010u);\n", ""))
        try:
            verify.verify(missing_entry, selection)
            raise AssertionError("missing selected entry was accepted")
        except ValueError as error:
            require("0 trace entries" in str(error), "orphaned exit failed for the wrong reason")
    print("guest trace structural and mutation tests passed")


def integration_tests() -> None:
    engine = os.environ.get("PROTODROID_ENGINE")
    require(bool(engine) and Path(engine).is_file(), "--integration requires PROTODROID_ENGINE")
    # Import here rather than silently skipping: the integration caller must provide the generator dependencies.
    import capstone  # noqa: F401
    import pefile  # noqa: F401
    verify = load_verify()
    selection = ROOT / "re/loading_boundary_trace.txt"
    expected = sorted(f"{rva:08x}" for rva in verify.selected_rvas(selection))
    with tempfile.TemporaryDirectory(prefix="protodroid-guest-trace-integration-") as temporary:
        root = Path(temporary)
        functions = root / "functions.txt"
        functions.write_text("".join(line for line in selection.read_text().splitlines(True)
                                     if line.split("#", 1)[0].strip()))
        outputs = {}
        for name, extra in (("default", []), ("traced", ["--trace-functions", str(selection)])):
            output = root / f"{name}.c"
            subprocess.run([
                sys.executable, str(ROOT / "tools/recomp/recomp.py"),
                "--functions", str(functions), "--base", "0x7c000000",
                *extra, "--out", str(output),
            ], cwd=ROOT, env=os.environ, check=True)
            outputs[name] = output.read_text()
        require("RecompGuestFunction" not in outputs["default"], "default generation gained trace hooks")
        enters = re.findall(r"RecompGuestFunctionEnter\(c, 0x([0-9a-f]{8})u\);", outputs["traced"])
        require(sorted(enters) == expected, f"actual trace entries {enters} != {expected}")
        restored = re.sub(r"^    RecompGuestFunctionEnter\(c, 0x[0-9a-f]{8}u\);\n", "",
                          outputs["traced"], flags=re.MULTILINE)
        restored = re.sub(r"RecompGuestFunctionExit\(c, 0x[0-9a-f]{8}u\); return;", "return;", restored)
        require(restored == outputs["default"], "removing instrumentation does not restore default source")
        require(verify.verify(root / "traced.c", selection)["functions"] == len(expected),
                "real generated probes failed verification")

        common = [
            "-std=c11", "-O1", "-DRECOMP_FLAT_MEMORY", "-DRECOMP_GUEST_BASE=0x1000000000ull",
            "-fsigned-char", "-fno-strict-aliasing", "-fwrapv", "-fno-delete-null-pointer-checks",
            "-w", f"-I{ROOT / 'src/recomp'}", "-fsyntax-only",
        ]
        compiler = os.environ.get("CC", "cc")
        for name in ("default", "traced"):
            subprocess.run([compiler, *common, str(root / f"{name}.c")], cwd=ROOT, check=True)

        stale = root / "stale.txt"
        stale.write_text("00000001\n")
        failed = subprocess.run([
            sys.executable, str(ROOT / "tools/recomp/recomp.py"), "--functions", str(functions),
            "--base", "0x7c000000", "--trace-functions", str(stale), "--out", str(root / "stale.c"),
        ], cwd=ROOT, env=os.environ, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        require(failed.returncode != 0 and "not a requested known entry" in failed.stdout,
                "stale trace address did not fail explicitly")

        duplicate = root / "duplicate.txt"
        duplicate.write_text("104e5250\n004e5250\n")
        failed = subprocess.run([
            sys.executable, str(ROOT / "tools/recomp/recomp.py"), "--functions", str(functions),
            "--base", "0x7c000000", "--trace-functions", str(duplicate), "--out", str(root / "duplicate.c"),
        ], cwd=ROOT, env=os.environ, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        require(failed.returncode != 0 and "duplicate trace function" in failed.stdout,
                "duplicate generator selection did not fail explicitly")
    print("guest trace real-DLL differential, syntax, and fail-fast tests passed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--integration", action="store_true", help="also generate/compile the supplied real DLL")
    args = parser.parse_args()
    structural_tests()
    if args.integration:
        integration_tests()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
