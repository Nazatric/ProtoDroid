#!/usr/bin/env python3
"""Exact aggregation and malformed-input tests for analyze_d3d9_timing.py."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("analyze_d3d9_timing", ROOT / "tools/analyze_d3d9_timing.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)

DRAW = "draw_total n=3 ok=1 false=1 errors=1 changed=1 flagged=1 units=3 total_ns=3000000 max_ns=2000000 avg=1.000 max=2.000ms"
STREAM = "stream_total n=4 ok=4 false=0 errors=0 changed=3 flagged=0 units=8 total_ns=500000 max_ns=125000 avg=0.125 max=0.125ms"
QUERY = "query_event n=1 ok=0 false=1 errors=0 changed=0 flagged=1 units=0 total_ns=500000 max_ns=500000 avg=0.500 max=0.500ms"
RESIDUAL = "draw_lock n=2 ok=2 false=0 errors=0 changed=0 flagged=0 units=0 total_ns=4000 max_ns=3000 avg=0.002 max=0.003ms"
BASE = "draw_total n=1 ok=1 false=0 errors=0 changed=0 flagged=0 units=0 total_ns=1000000 max_ns=1000000 avg=1.000 max=1.000ms"


def check(condition, message):
    if not condition:
        raise AssertionError(message)


with tempfile.TemporaryDirectory(prefix="d3d9-timing-analysis-test-") as temp_name:
    temp = Path(temp_name)
    previous = temp / "native.prev.log"
    current = temp / "native.log"
    previous_bytes = ("ignored prefix\ninfo: DXVK D3D9 timing: " + DRAW + "; " + STREAM + "\n").encode()
    current_bytes = ("logcat DXVK D3D9 timing: " + QUERY + "\n"
                     "DXVK D3D9 timing final (device_destroy): " + RESIDUAL + "\n"
                     "DXVK D3D9 timing final (empty): \n").encode()
    previous.write_bytes(previous_bytes)
    current.write_bytes(current_bytes)
    report = MODULE.analyze([previous, current])
    check(report["status"] == "valid-exact-records" and report["window_count"] == 4, "window parsing")
    check(report["operation_record_count"] == 4 and report["final_flush_observed"], "residual/final accounting")
    check(report["input_files"][0]["sha256"] == hashlib.sha256(previous_bytes).hexdigest() and
          report["input_files"][1]["sha256"] == hashlib.sha256(current_bytes).hexdigest(), "input hashes")
    draw = report["operations"]["draw_total"]
    check(draw["calls"] == 3 and draw["total_ns"] == 3000000 and draw["max_ns"] == 2000000, "exact draw totals")
    check(draw["average_ms"] == 1.0 and draw["changed_ratio"] == 1 / 3 and draw["flagged_ratio"] == 1 / 3,
          "exact ratios")
    stream = report["operations"]["stream_total"]
    check(stream["changed"] == 3 and stream["changed_ratio"] == 0.75 and stream["units"] == 8,
          "changed-state opportunity measurement")
    check(report["operations"]["draw_lock"]["total_ns"] == 4000 and
          report["operations"]["query_event"]["false"] == 1, "residual and query results")
    check("total_frame_ns" not in report and "total_operation_ns" not in report, "nested times were not added")
    output = temp / "report.json"
    result = subprocess.run([sys.executable, str(ROOT / "tools/analyze_d3d9_timing.py"),
                             str(previous), str(current), "--output", str(output)],
                            cwd=ROOT, text=True, capture_output=True)
    check(result.returncode == 0 and json.loads(result.stdout) == report and output.read_text() == result.stdout,
          "CLI/output JSON equivalence")

    anomaly_file = temp / "anomaly.log"
    anomaly_file.write_text("DXVK D3D9 timing: draw_total n=1 ok=0 false=0 errors=0 changed=2 flagged=2 "
                            "units=0 total_ns=0 max_ns=0 avg=0.000 max=0.000ms\n")
    anomaly = MODULE.analyze([anomaly_file])["operations"]["draw_total"]
    check(anomaly["result_accounting_delta"] == 1 and len(anomaly["anomalies"]) == 3,
          "relaxed-counter anomalies are visible rather than rejected or hidden")

    malformed = {
        "missing exact total": BASE.replace(" total_ns=1000000", ""),
        "average inconsistency": BASE.replace("avg=1.000", "avg=1.001"),
        "maximum inconsistency": BASE.replace("max=1.000ms", "max=1.001ms"),
        "duplicate operation": BASE + "; " + BASE,
        "unknown operation": BASE.replace("draw_total", "made_up", 1),
        "trailing data": BASE + " trailing",
        "zero calls": "draw_total n=0 ok=0 false=0 errors=0 changed=0 flagged=0 units=0 total_ns=0 max_ns=0 avg=0.000 max=0.000ms",
        "negative total": BASE.replace("total_ns=1000000", "total_ns=-1"),
    }
    checks = 12
    for description, payload in malformed.items():
        path = temp / "bad.log"
        path.write_text("DXVK D3D9 timing: " + payload + "\n")
        try:
            MODULE.analyze([path])
        except ValueError:
            pass
        else:
            raise AssertionError(f"malformed input accepted: {description}")
        checks += 1
    for description, content in {
        "empty periodic": "DXVK D3D9 timing: \n",
        "malformed final": "DXVK D3D9 timing final (device_destroy: " + BASE + "\n",
        "no records": "unrelated log\n",
    }.items():
        path = temp / "bad-other.log"
        path.write_text(content)
        try:
            MODULE.analyze([path])
        except ValueError:
            pass
        else:
            raise AssertionError(f"malformed input accepted: {description}")
        checks += 1
    invalid_utf8 = temp / "invalid.log"
    invalid_utf8.write_bytes(b"\xff")
    result = subprocess.run([sys.executable, str(ROOT / "tools/analyze_d3d9_timing.py"), str(invalid_utf8)],
                            cwd=ROOT, text=True, capture_output=True)
    check(result.returncode == 2 and result.stderr.startswith("error:"), "invalid UTF-8 CLI rejection")
    checks += 1
print(f"D3D9 timing analyzer tests passed ({checks} checks; exact totals/ratios, residuals, anomalies, CLI, 12 malformed classes)")
