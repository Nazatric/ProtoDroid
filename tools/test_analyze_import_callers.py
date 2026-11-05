#!/usr/bin/env python3
"""Symbolization, accounting, CLI, and malformed-input tests for import-caller analysis."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("analyze_import_callers", ROOT / "tools/analyze_import_callers.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def functions_csv(rows):
    text = "entry,size,is_thunk,namespace,name,source_files\n"
    for entry, size, namespace, name in rows:
        text += f"{entry},{size},false,{namespace},{name},\n"
    return text


with tempfile.TemporaryDirectory(prefix="import-caller-analysis-test-") as temp_name:
    temp = Path(temp_name)
    functions = temp / "functions.csv"
    functions.write_text(functions_csv([
        ("108a6070", 33, "engine::streampackagestates::Loading", "Loading"),
        ("108a6140", 747, "engine::streampackagestates::Loading", "vfunction3"),
    ]))
    previous = temp / "native.prev.log"
    current = temp / "native.log"
    previous_bytes = (("prefix import-callers tid 7 method IDirect3DDevice9::DrawIndexedPrimitive "
                       "total_ns=2000 total_calls=4 caller=008a6151:1000:2 caller=ffffffff:500:1 "
                       "unlisted_ns=500 unlisted_calls=1\n").encode())
    current_bytes = (("import-callers tid 7 method IDirect3DDevice9::DrawIndexedPrimitive "
                      "total_ns=1500 total_calls=3 caller=008a6151:900:1 caller=00000010:100:1 "
                      "unlisted_ns=500 unlisted_calls=1\n"
                      "import-callers tid 7 method IDirect3DDevice9::SetIndices "
                      "total_ns=100 total_calls=1 caller=008a6071:100:1 unlisted_ns=0 unlisted_calls=0\n").encode())
    previous.write_bytes(previous_bytes)
    current.write_bytes(current_bytes)
    report = MODULE.analyze([previous, current], functions)
    check(report["record_count"] == 3 and report["method_count"] == 2, "record/method aggregation")
    check(report["input_files"][0]["sha256"] == hashlib.sha256(previous_bytes).hexdigest() and
          report["input_files"][1]["sha256"] == hashlib.sha256(current_bytes).hexdigest(), "input hashes")
    check(report["functions_csv"]["function_extents"] == 2, "function extent loading")
    draw = next(method for method in report["methods"] if method["method"].endswith("DrawIndexedPrimitive"))
    check(draw["total_ns"] == 3500 and draw["total_calls"] == 7 and draw["unlisted_calls"] == 2,
          "exact method totals")
    loading = next(caller for caller in draw["callers"] if caller["caller_rva"] == "008a6151")
    check(loading["total_ns"] == 1900 and loading["calls"] == 3 and
          loading["resolution"] == "containing-function" and
          loading["symbol"]["name"] == "engine::streampackagestates::Loading::vfunction3" and
          loading["symbol"]["entry"] == "108a6140" and loading["symbol"]["call_site_rva"] == "008a6150",
          "containing-function symbolization")
    check(next(caller for caller in draw["callers"] if caller["caller_rva"] == "ffffffff")["resolution"] == "external" and
          next(caller for caller in draw["callers"] if caller["caller_rva"] == "00000010")["resolution"] == "unresolved",
          "external/unresolved visibility")
    counts = report["resolution_call_counts"]
    check(counts == {"containing-function_calls": 4, "external_calls": 1, "unlisted_calls": 2,
                     "unresolved_calls": 1}, "complete resolution accounting")
    output = temp / "report.json"
    result = subprocess.run([sys.executable, str(ROOT / "tools/analyze_import_callers.py"),
                             str(previous), str(current), "--functions", str(functions), "--output", str(output)],
                            cwd=ROOT, text=True, capture_output=True)
    check(result.returncode == 0 and json.loads(result.stdout) == report and output.read_text() == result.stdout,
          "CLI/output equivalence")

    valid = "import-callers tid 1 method M total_ns=10 total_calls=1 caller=008a6151:10:1 unlisted_ns=0 unlisted_calls=0"
    malformed = {
        "accounting mismatch": valid.replace("total_ns=10", "total_ns=11"),
        "duplicate caller": valid.replace("unlisted_ns=", "caller=008a6151:0:1 unlisted_ns=", 1),
        "missing residual": valid.replace(" unlisted_ns=0 unlisted_calls=0", ""),
        "negative residual": valid.replace("unlisted_ns=0", "unlisted_ns=-1"),
        "bad caller tuple": valid.replace("caller=008a6151:10:1", "caller=xyz"),
        "zero calls": valid.replace("total_calls=1", "total_calls=0"),
        "trailing field": valid + " extra=1",
        "four callers": "import-callers tid 1 method M total_ns=4 total_calls=4 "
                        "caller=00000001:1:1 caller=00000002:1:1 caller=00000003:1:1 caller=00000004:1:1 "
                        "unlisted_ns=0 unlisted_calls=0",
    }
    checks = 10
    for description, content in malformed.items():
        path = temp / "bad.log"
        path.write_text(content + "\n")
        try:
            MODULE.analyze([path], functions)
        except ValueError:
            pass
        else:
            raise AssertionError(f"malformed record accepted: {description}")
        checks += 1
    empty = temp / "empty.log"
    empty.write_text("unrelated\n")
    try:
        MODULE.analyze([empty], functions)
    except ValueError:
        pass
    else:
        raise AssertionError("empty input accepted")
    checks += 1

    ambiguous_functions = temp / "ambiguous.csv"
    ambiguous_functions.write_text(functions_csv([
        ("10002000", 32, "Global", "FUN_one"), ("10002000", 32, "Global", "FUN_two")]))
    ambiguous_log = temp / "ambiguous.log"
    ambiguous_log.write_text("import-callers tid 1 method M total_ns=1 total_calls=1 "
                             "caller=00002001:1:1 unlisted_ns=0 unlisted_calls=0\n")
    ambiguous = MODULE.analyze([ambiguous_log], ambiguous_functions)
    check(ambiguous["methods"][0]["callers"][0]["resolution"] == "ambiguous", "ambiguous extent was guessed")
    outside_log = temp / "outside.log"
    outside_log.write_text("import-callers tid 1 method M total_ns=1 total_calls=1 "
                           "caller=008a6441:1:1 unlisted_ns=0 unlisted_calls=0\n")
    outside = MODULE.analyze([outside_log], functions)
    check(outside["methods"][0]["callers"][0]["resolution"] == "unresolved", "out-of-extent caller was guessed")
    checks += 2
    invalid = temp / "invalid.log"
    invalid.write_bytes(b"\xff")
    result = subprocess.run([sys.executable, str(ROOT / "tools/analyze_import_callers.py"), str(invalid),
                             "--functions", str(functions)], cwd=ROOT, text=True, capture_output=True)
    check(result.returncode == 2 and result.stderr.startswith("error:"), "invalid UTF-8 CLI rejection")
    checks += 1
print(f"Import-caller analyzer tests passed ({checks} checks; symbols, exact residuals, CLI, 10 malformed classes)")
