#!/usr/bin/env python3
"""Symbolize and aggregate bounded ProtoDroid import-callers diagnostic records."""
from __future__ import annotations

import argparse
import bisect
import csv
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re
import sys

IMAGE_BASE = 0x10000000
HEADER_RE = re.compile(
    r"import-callers tid (?P<tid>\d+) method (?P<method>\S+) "
    r"total_ns=(?P<total_ns>\d+) total_calls=(?P<total_calls>\d+)(?P<body>.*)$"
)
CALLER_RE = re.compile(r"caller=(?P<rva>[0-9a-f]{8}):(?P<ns>\d+):(?P<calls>\d+)")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class Symbolizer:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        self.starts = []
        self.rows = []
        for row in csv.DictReader(self.data.decode("utf-8").splitlines()):
            entry = int(row["entry"], 16)
            size = int(row["size"])
            if entry <= IMAGE_BASE or size <= 0:
                continue
            name = row["name"].strip()
            namespace = row["namespace"].strip()
            qualified = name if not namespace or namespace == "Global" else namespace + "::" + name
            self.rows.append({"start": entry - IMAGE_BASE, "end": entry - IMAGE_BASE + size,
                              "name": qualified, "entry": entry, "size": size})
        self.rows.sort(key=lambda row: (row["start"], row["end"], row["name"]))
        self.starts = [row["start"] for row in self.rows]

    def resolve(self, caller_rva: int) -> dict:
        if caller_rva == 0xFFFFFFFF:
            return {"resolution": "external", "symbol": None}
        if caller_rva == 0:
            return {"resolution": "unresolved", "symbol": None}
        call_site = caller_rva - 1
        index = bisect.bisect_right(self.starts, call_site) - 1
        if index < 0:
            return {"resolution": "unresolved", "symbol": None}
        start = self.rows[index]["start"]
        while index > 0 and self.rows[index - 1]["start"] == start:
            index -= 1
        candidates = []
        while index < len(self.rows) and self.rows[index]["start"] == start:
            row = self.rows[index]
            if row["start"] <= call_site < row["end"]:
                candidates.append(row)
            index += 1
        if len(candidates) != 1:
            return {"resolution": "ambiguous" if candidates else "unresolved", "symbol": None}
        row = candidates[0]
        return {
            "resolution": "containing-function",
            "symbol": {"name": row["name"], "entry": f"{row['entry']:08x}", "size": row["size"],
                       "call_site_rva": f"{call_site:08x}"},
        }


def parse_line(line: str, source: Path, line_number: int) -> dict:
    match = HEADER_RE.search(line)
    if match is None:
        raise ValueError(f"{source}:{line_number}: malformed import-callers record")
    total_ns = int(match.group("total_ns"))
    total_calls = int(match.group("total_calls"))
    if total_calls == 0:
        raise ValueError(f"{source}:{line_number}: zero-call method record")
    tokens = match.group("body").strip().split()
    callers = []
    seen = set()
    position = 0
    while position < len(tokens) and tokens[position].startswith("caller="):
        caller = CALLER_RE.fullmatch(tokens[position])
        if caller is None:
            raise ValueError(f"{source}:{line_number}: malformed caller tuple")
        rva = int(caller.group("rva"), 16)
        if rva in seen:
            raise ValueError(f"{source}:{line_number}: duplicate caller {rva:08x}")
        seen.add(rva)
        calls = int(caller.group("calls"))
        if calls == 0:
            raise ValueError(f"{source}:{line_number}: zero-call caller")
        callers.append({"caller_rva": rva, "total_ns": int(caller.group("ns")), "calls": calls})
        position += 1
    if len(callers) > 3:
        raise ValueError(f"{source}:{line_number}: more than three listed callers")
    if position + 2 != len(tokens) or not tokens[position].startswith("unlisted_ns=") or \
            not tokens[position + 1].startswith("unlisted_calls="):
        raise ValueError(f"{source}:{line_number}: missing or trailing unlisted totals")
    try:
        unlisted_ns = int(tokens[position].split("=", 1)[1])
        unlisted_calls = int(tokens[position + 1].split("=", 1)[1])
    except ValueError as error:
        raise ValueError(f"{source}:{line_number}: malformed unlisted totals") from error
    if unlisted_ns < 0 or unlisted_calls < 0:
        raise ValueError(f"{source}:{line_number}: negative unlisted totals")
    if sum(item["total_ns"] for item in callers) + unlisted_ns != total_ns or \
            sum(item["calls"] for item in callers) + unlisted_calls != total_calls:
        raise ValueError(f"{source}:{line_number}: listed plus unlisted accounting mismatch")
    return {"tid": int(match.group("tid")), "method": match.group("method"), "total_ns": total_ns,
            "total_calls": total_calls, "callers": callers, "unlisted_ns": unlisted_ns,
            "unlisted_calls": unlisted_calls}


def analyze(log_paths: list[Path], functions_path: Path) -> dict:
    symbolizer = Symbolizer(functions_path)
    inputs = []
    records = []
    methods = defaultdict(lambda: {"total_ns": 0, "total_calls": 0, "unlisted_ns": 0, "unlisted_calls": 0,
                                   "callers": defaultdict(lambda: {"total_ns": 0, "calls": 0})})
    for path in log_paths:
        data = path.read_bytes()
        info = {"path": str(path), "bytes": len(data), "sha256": sha256(data), "record_count": 0}
        inputs.append(info)
        for line_number, line in enumerate(data.decode("utf-8").splitlines(), 1):
            if "import-callers" not in line:
                continue
            record = parse_line(line, path, line_number)
            record.update({"index": len(records), "source": str(path), "line": line_number})
            records.append(record)
            info["record_count"] += 1
            key = (record["tid"], record["method"])
            aggregate = methods[key]
            for field in ("total_ns", "total_calls", "unlisted_ns", "unlisted_calls"):
                aggregate[field] += record[field]
            for caller in record["callers"]:
                stats = aggregate["callers"][caller["caller_rva"]]
                stats["total_ns"] += caller["total_ns"]
                stats["calls"] += caller["calls"]
    if not records:
        raise ValueError("no import-callers records found")
    methods_out = []
    resolution_counts = defaultdict(int)
    for (tid, method), aggregate in sorted(methods.items(), key=lambda item: (-item[1]["total_ns"], item[0])):
        callers_out = []
        for rva, stats in sorted(aggregate["callers"].items(), key=lambda item: (-item[1]["total_ns"], item[0])):
            resolution = symbolizer.resolve(rva)
            resolution_counts[resolution["resolution"] + "_calls"] += stats["calls"]
            callers_out.append({"caller_rva": f"{rva:08x}", **stats, **resolution})
        methods_out.append({"tid": tid, "method": method,
                            **{key: value for key, value in aggregate.items() if key != "callers"},
                            "listed_caller_count": len(callers_out), "callers": callers_out})
    resolution_counts["unlisted_calls"] = sum(method["unlisted_calls"] for method in methods_out)
    return {
        "schema_version": 1,
        "status": "valid-import-caller-records",
        "input_files": inputs,
        "functions_csv": {"path": str(functions_path), "sha256": sha256(symbolizer.data),
                          "function_extents": len(symbolizer.rows)},
        "record_count": len(records),
        "method_count": len(methods_out),
        "resolution_call_counts": dict(sorted(resolution_counts.items())),
        "methods": methods_out,
        "records": records,
        "limitations": [
            "Only the existing import profiler's top-ten methods per thread/window emit caller records; this is not a complete census.",
            "At most three callers are listed per method/window; unlisted and overflow residuals remain aggregated and are not symbolized.",
            "Caller RVAs name the immediate recompiled call site, not a whole guest stack or root cause.",
            "Inclusive nested import durations are not GPU time and must not be summed into frame time.",
        ],
    }


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=Path, help="log files in chronological order")
    parser.add_argument("--functions", type=Path, default=root / "re/functions.csv")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        report = analyze(args.inputs, args.functions)
        rendered = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if args.output:
            args.output.write_text(rendered, encoding="utf-8")
        sys.stdout.write(rendered)
        return 0
    except (OSError, UnicodeError, ValueError, KeyError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
