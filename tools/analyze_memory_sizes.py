#!/usr/bin/env python3
"""Validate and aggregate opt-in ProtoDroid CRT memory-size records; this is not a timing/FPS analyzer."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path

LABELS = (
    "0", "1-8", "9-16", "17-32", "33-64", "65-128", "129-256", "257-1024",
    "1025-4096", "4097-16384", "16385-65536", "65537-262144", "262145-1048576", "1048577+",
)
BOUNDS = (
    (0, 0), (1, 8), (9, 16), (17, 32), (33, 64), (65, 128), (129, 256), (257, 1024),
    (1025, 4096), (4097, 16384), (16385, 65536), (65537, 262144), (262145, 1048576), (1048577, None),
)
HEADER = re.compile(
    r"memory-sizes tid (\d+) name=(memcpy|memmove|memset) calls=(\d+) import_calls=(\d+) "
    r"total_bytes=(\d+) min=(\d+) max=(\d+) bins=(\S+)"
)
MONO = re.compile(r"^\[mono=([0-9.]+) tid=(\d+)\]")


def parse_record(line: str, source: str, line_number: int) -> dict:
    match = HEADER.search(line)
    if not match:
        raise ValueError(f"{source}:{line_number}: malformed memory-sizes record")
    tid, name, calls, import_calls, total_bytes, minimum, maximum, encoded_bins = match.groups()
    calls, import_calls, total_bytes, minimum, maximum = map(int, (calls, import_calls, total_bytes, minimum, maximum))
    pieces = encoded_bins.split(",")
    if len(pieces) != len(LABELS):
        raise ValueError(f"{source}:{line_number}: wrong bin count")
    counts = []
    for index, (piece, label) in enumerate(zip(pieces, LABELS)):
        prefix = label + ":"
        if not piece.startswith(prefix) or not piece[len(prefix):].isdigit():
            raise ValueError(f"{source}:{line_number}: invalid/misordered bin {index}")
        counts.append(int(piece[len(prefix):]))
    if calls <= 0 or import_calls != calls or sum(counts) != calls:
        raise ValueError(f"{source}:{line_number}: call/import/bin accounting mismatch")
    if minimum > maximum or not (calls * minimum <= total_bytes <= calls * maximum):
        raise ValueError(f"{source}:{line_number}: impossible min/max/byte total")
    populated = [index for index, count in enumerate(counts) if count]
    if not populated:
        raise ValueError(f"{source}:{line_number}: no populated bins")
    for value, bin_index, description in ((minimum, populated[0], "minimum"), (maximum, populated[-1], "maximum")):
        low, high = BOUNDS[bin_index]
        if value < low or (high is not None and value > high):
            raise ValueError(f"{source}:{line_number}: {description} is outside its populated bin")
    mono = MONO.match(line)
    return {
        "source": source,
        "line": line_number,
        "mono_s": float(mono.group(1)) if mono else None,
        "tid": int(tid),
        "name": name,
        "calls": calls,
        "import_calls": import_calls,
        "total_bytes": total_bytes,
        "min": minimum,
        "max": maximum,
        "bins": dict(zip(LABELS, counts)),
    }


def analyze(paths: list[str | Path]) -> dict:
    files = []
    records = []
    for path_value in paths:
        path = Path(path_value)
        raw = path.read_bytes()
        text = raw.decode("utf-8", errors="replace")
        file_records = [parse_record(line, str(path), number)
                        for number, line in enumerate(text.splitlines(), 1) if "memory-sizes tid " in line]
        files.append({"path": str(path), "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest(),
                      "records": len(file_records)})
        records.extend(file_records)
    operations = {}
    for name in ("memcpy", "memmove", "memset"):
        selected = [record for record in records if record["name"] == name]
        if not selected:
            continue
        calls = sum(record["calls"] for record in selected)
        total_bytes = sum(record["total_bytes"] for record in selected)
        bins = {label: sum(record["bins"][label] for record in selected) for label in LABELS}
        operations[name] = {
            "records": len(selected),
            "threads": sorted({record["tid"] for record in selected}),
            "calls": calls,
            "total_bytes": total_bytes,
            "average_bytes_per_call": total_bytes / calls,
            "min": min(record["min"] for record in selected),
            "max": max(record["max"] for record in selected),
            "bins": bins,
        }
        assert sum(bins.values()) == calls
    return {
        "format": 1,
        "files": files,
        "record_count": len(records),
        "records": records,
        "operations": operations,
        "note": "Exact opt-in call-size accounting only; no duration, FPS, causation, or optimization conclusion is inferred.",
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+")
    parser.add_argument("--output")
    args = parser.parse_args()
    report = json.dumps(analyze(args.logs), indent=2, sort_keys=True) + "\n"
    if args.output:
        Path(args.output).write_text(report, encoding="utf-8")
        print(f"Wrote {args.output}")
    else:
        print(report, end="")


if __name__ == "__main__":
    main()
