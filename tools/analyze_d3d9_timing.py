#!/usr/bin/env python3
"""Aggregate ProtoDroid's exact DXVK D3D9 timing records without adding nested times."""
from __future__ import annotations

import argparse
from collections import defaultdict
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import re
import sys

NORMAL_MARKER = "DXVK D3D9 timing: "
FINAL_RE = re.compile(r"DXVK D3D9 timing final \((?P<reason>[^()]*)\): (?P<payload>.*)$")
STAT_RE = re.compile(
    r"(?P<op>[a-z_]+) n=(?P<n>\d+) ok=(?P<ok>\d+) false=(?P<false>\d+) "
    r"errors=(?P<errors>\d+) changed=(?P<changed>\d+) flagged=(?P<flagged>\d+) "
    r"units=(?P<units>\d+) total_ns=(?P<total_ns>\d+) max_ns=(?P<max_ns>\d+) "
    r"avg=(?P<avg>\d+\.\d{3}) max=(?P<max>\d+\.\d{3})ms"
)
COUNT_FIELDS = ("n", "ok", "false", "errors", "changed", "flagged", "units", "total_ns", "max_ns")
KNOWN_OPS = {
    "draw_lock", "draw_total", "draw_upload", "prepare_draw", "draw_emit",
    "stream_lock", "stream_total", "indices_lock", "indices_total", "texture_lock", "texture_total",
    "query_lock", "query_event", "query_occlusion", "query_timestamp", "query_disjoint", "query_other",
}
ROUNDING_TOLERANCE = Decimal("0.000500001")


def parse_payload(payload: str, source: str, line_number: int) -> list[dict]:
    if not payload:
        raise ValueError(f"{source}:{line_number}: empty periodic timing record")
    operations = []
    seen = set()
    for item in payload.split("; "):
        match = STAT_RE.fullmatch(item)
        if match is None:
            raise ValueError(f"{source}:{line_number}: malformed or non-exact timing operation: {item!r}")
        values = {field: int(match.group(field)) for field in COUNT_FIELDS}
        op = match.group("op")
        if op not in KNOWN_OPS:
            raise ValueError(f"{source}:{line_number}: unknown timing operation {op}")
        if op in seen:
            raise ValueError(f"{source}:{line_number}: duplicate operation {op}")
        seen.add(op)
        if values["n"] == 0:
            raise ValueError(f"{source}:{line_number}: zero-call operation {op}")
        exact_avg = Decimal(values["total_ns"]) / Decimal(values["n"]) / Decimal(1000000)
        exact_max = Decimal(values["max_ns"]) / Decimal(1000000)
        if abs(Decimal(match.group("avg")) - exact_avg) > ROUNDING_TOLERANCE:
            raise ValueError(f"{source}:{line_number}: rounded average disagrees with total_ns for {op}")
        if abs(Decimal(match.group("max")) - exact_max) > ROUNDING_TOLERANCE:
            raise ValueError(f"{source}:{line_number}: rounded maximum disagrees with max_ns for {op}")
        operations.append({"op": op, **values})
    return operations


def analyze(paths: list[Path]) -> dict:
    inputs = []
    windows = []
    aggregates = defaultdict(lambda: {field: 0 for field in COUNT_FIELDS})
    first_last = {}
    for path in paths:
        data = path.read_bytes()
        text = data.decode("utf-8")
        input_info = {"path": str(path), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                      "timing_lines": 0}
        inputs.append(input_info)
        for line_number, line in enumerate(text.splitlines(), 1):
            final = FINAL_RE.search(line)
            if final is not None:
                payload = final.group("payload")
                reason = final.group("reason")
                operations = [] if not payload else parse_payload(payload, str(path), line_number)
                is_final = True
            elif NORMAL_MARKER in line:
                payload = line.split(NORMAL_MARKER, 1)[1]
                reason = None
                operations = parse_payload(payload, str(path), line_number)
                is_final = False
            elif "DXVK D3D9 timing" in line:
                raise ValueError(f"{path}:{line_number}: unrecognized DXVK D3D9 timing line")
            else:
                continue
            input_info["timing_lines"] += 1
            window_index = len(windows)
            window = {"index": window_index, "source": str(path), "line": line_number,
                      "final": is_final, "reason": reason, "operations": operations}
            windows.append(window)
            for operation in operations:
                op = operation["op"]
                for field in COUNT_FIELDS:
                    aggregates[op][field] += operation[field]
                if op not in first_last:
                    first_last[op] = [window_index, window_index]
                else:
                    first_last[op][1] = window_index
    if not windows or not any(window["operations"] for window in windows):
        raise ValueError("no exact DXVK D3D9 timing operations found")
    operations_out = {}
    for op in sorted(aggregates):
        values = dict(aggregates[op])
        calls = values["n"]
        accounted = values["ok"] + values["false"] + values["errors"]
        anomalies = []
        delta = calls - accounted
        if delta:
            anomalies.append("relaxed result counters do not exactly match calls")
        if values["changed"] > calls:
            anomalies.append("relaxed changed counter exceeds calls")
        if values["flagged"] > calls:
            anomalies.append("relaxed flagged counter exceeds calls")
        operations_out[op] = {
            "calls": calls,
            "ok": values["ok"],
            "false": values["false"],
            "errors": values["errors"],
            "changed": values["changed"],
            "flagged": values["flagged"],
            "units": values["units"],
            "total_ns": values["total_ns"],
            "max_ns": values["max_ns"],
            "average_ms": values["total_ns"] / calls / 1.0e6,
            "max_ms": values["max_ns"] / 1.0e6,
            "changed_ratio": values["changed"] / calls,
            "flagged_ratio": values["flagged"] / calls,
            "result_accounting_delta": delta,
            "window_count": sum(1 for window in windows if any(item["op"] == op for item in window["operations"])),
            "first_window": first_last[op][0],
            "last_window": first_last[op][1],
            "anomalies": anomalies,
        }
    return {
        "schema_version": 1,
        "status": "valid-exact-records",
        "input_files": inputs,
        "window_count": len(windows),
        "operation_record_count": sum(len(window["operations"]) for window in windows),
        "final_flush_observed": bool(windows and windows[-1]["final"]),
        "operations": operations_out,
        "windows": windows,
        "limitations": [
            "Method, lock, upload, prepare, and emit durations are inclusive and may be nested; no cross-operation total is computed.",
            "These are CPU/wall durations around D3D9 methods, not GPU timestamps or whole-frame stall attribution.",
            "Relaxed per-counter snapshots can split one operation across adjacent windows; anomalies are reported, not hidden.",
            "Aggregate averages use exact total_ns/calls; per-record avg/max text is validated only as rounded display output.",
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=Path, help="log files in chronological order (.prev before current)")
    parser.add_argument("--output", type=Path, help="also write the JSON report to this path")
    args = parser.parse_args()
    try:
        report = analyze(args.inputs)
        rendered = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if args.output:
            args.output.write_text(rendered, encoding="utf-8")
        sys.stdout.write(rendered)
        return 0
    except (OSError, UnicodeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
