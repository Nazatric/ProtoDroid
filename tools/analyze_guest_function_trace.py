#!/usr/bin/env python3
"""Validate, symbolize, and aggregate ProtoDroid guest-function-v1 diagnostic records."""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import csv
import hashlib
import json
from pathlib import Path
import re
import sys

IMAGE_BASE = 0x10000000
MARKER = re.compile(r"guest-function-v1 event=(?P<event>[a-z-]+)(?P<body>(?: [^\r\n]*)?)$")
COMMON_FIELDS = {
    "sequence", "rva", "start_ns", "end_ns", "duration_ns", "depth", "discarded_inner",
    "clock_regression", "ecx", "caller", "arg0", "arg1", "arg2",
}
HEX_FIELDS = {"rva", "ecx", "caller", "arg0", "arg1", "arg2"}
UINT_FIELDS = COMMON_FIELDS - HEX_FIELDS


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def integer(token: str, field: str, source: Path, line_number: int) -> int:
    try:
        value = int(token, 16 if field in HEX_FIELDS else 10)
    except ValueError as error:
        raise ValueError(f"{source}:{line_number}: invalid {field}") from error
    minimum = 1 if field == "sequence" else 0
    maximum = 0xffffffff if field in HEX_FIELDS else 0xffffffffffffffff
    if not minimum <= value <= maximum:
        raise ValueError(f"{source}:{line_number}: out-of-range {field}")
    return value


def tokens(body: str, source: Path, line_number: int) -> dict[str, str]:
    result = {}
    for token in body.strip().split():
        if token.count("=") != 1:
            raise ValueError(f"{source}:{line_number}: malformed token {token!r}")
        key, value = token.split("=", 1)
        if not key or not value or key in result:
            raise ValueError(f"{source}:{line_number}: duplicate/empty field {key!r}")
        result[key] = value
    return result


def parse_record(line: str, source: Path, line_number: int, sequence_seen: set[int]) -> dict | None:
    match = MARKER.search(line)
    if match is None:
        return None
    kind = match.group("event")
    fields = tokens(match.group("body"), source, line_number)
    if kind == "cap":
        if fields != {"suppressed_records_at_least": "1"}:
            raise ValueError(f"{source}:{line_number}: malformed cap record")
        return {"event": kind, "source": str(source), "line": line_number}
    if "sequence" not in fields:
        raise ValueError(f"{source}:{line_number}: missing sequence")
    sequence = integer(fields["sequence"], "sequence", source, line_number)
    if sequence in sequence_seen:
        raise ValueError(f"{source}:{line_number}: duplicate sequence {sequence}")
    sequence_seen.add(sequence)
    if kind == "drop":
        if set(fields) != {"sequence", "dropped_events"}:
            raise ValueError(f"{source}:{line_number}: malformed drop fields")
        dropped = integer(fields["dropped_events"], "dropped_events", source, line_number)
        if dropped == 0:
            raise ValueError(f"{source}:{line_number}: zero drop residual")
        return {"event": kind, "sequence": sequence, "dropped_events": dropped,
                "source": str(source), "line": line_number}
    if kind not in {"complete", "overflow-enter", "overflow-exit", "mismatch"}:
        raise ValueError(f"{source}:{line_number}: unknown event {kind}")
    if set(fields) != COMMON_FIELDS:
        raise ValueError(f"{source}:{line_number}: wrong {kind} fields")
    record = {"event": kind, "source": str(source), "line": line_number}
    for field in sorted(COMMON_FIELDS):
        record[field] = integer(fields[field], field, source, line_number)
    if record["rva"] == 0:
        raise ValueError(f"{source}:{line_number}: zero function RVA")
    if record["clock_regression"] not in (0, 1):
        raise ValueError(f"{source}:{line_number}: invalid clock_regression")
    if kind == "complete":
        if record["clock_regression"]:
            if record["end_ns"] >= record["start_ns"] or record["duration_ns"] != 0:
                raise ValueError(f"{source}:{line_number}: inconsistent regressed clock")
        elif record["end_ns"] < record["start_ns"] or \
                record["duration_ns"] != record["end_ns"] - record["start_ns"]:
            raise ValueError(f"{source}:{line_number}: duration/timestamp mismatch")
    elif record["duration_ns"] != 0:
        raise ValueError(f"{source}:{line_number}: non-complete event has a duration")
    return record


def symbols(path: Path) -> tuple[dict[int, list[str]], bytes]:
    data = path.read_bytes()
    result: dict[int, list[str]] = defaultdict(list)
    for row in csv.DictReader(data.decode("utf-8").splitlines()):
        entry = int(row["entry"], 16)
        if entry <= IMAGE_BASE:
            continue
        namespace = row["namespace"].strip()
        name = row["name"].strip()
        qualified = name if not namespace or namespace == "Global" else namespace + "::" + name
        result[entry - IMAGE_BASE].append(qualified)
    return dict(result), data


def percentile(values: list[int], percent: int) -> int:
    ordered = sorted(values)
    rank = max(1, (percent * len(ordered) + 99) // 100)
    return ordered[rank - 1]


def analyze(paths: list[Path], functions_path: Path) -> dict:
    symbol_rows, symbol_data = symbols(functions_path)
    inputs = []
    records = []
    for path in paths:
        data = path.read_bytes()
        info = {"path": str(path), "bytes": len(data), "sha256": sha256(data), "record_count": 0}
        seen = set()
        for line_number, line in enumerate(data.decode("utf-8").splitlines(), 1):
            record = parse_record(line, path, line_number, seen)
            if record is None:
                continue
            records.append(record)
            info["record_count"] += 1
        inputs.append(info)
    if not records:
        raise ValueError("no guest-function-v1 records found")

    durations: dict[int, list[int]] = defaultdict(list)
    requests: Counter[tuple[int, int, int]] = Counter()
    anomalies = Counter()
    for record in records:
        kind = record["event"]
        if kind != "complete":
            anomalies[kind] += 1
            continue
        for field in ("clock_regression", "discarded_inner"):
            if record[field]:
                anomalies[field] += 1
        durations[record["rva"]].append(record["duration_ns"])
        if record["rva"] == 0x838b40:
            requests[(record["arg0"], record["arg1"], record["arg2"])] += 1

    functions = []
    for rva, values in sorted(durations.items()):
        names = symbol_rows.get(rva, [])
        functions.append({
            "rva": f"{rva:08x}",
            "symbol": names[0] if len(names) == 1 else None,
            "symbol_resolution": "unique" if len(names) == 1 else "ambiguous" if names else "unresolved",
            "calls": len(values),
            "total_ns": sum(values),
            "min_ns": min(values),
            "p50_ns": percentile(values, 50),
            "p95_ns": percentile(values, 95),
            "p99_ns": percentile(values, 99),
            "max_ns": max(values),
        })
    anomalous = bool(anomalies)
    return {
        "schema_version": 1,
        "status": "anomalous-guest-function-records" if anomalous else "valid-guest-function-records",
        "input_files": inputs,
        "functions_csv": {"path": str(functions_path), "sha256": sha256(symbol_data)},
        "record_count": len(records),
        "complete_count": sum(len(values) for values in durations.values()),
        "anomaly_counts": dict(sorted(anomalies.items())),
        "functions": functions,
        "game_state_request_wrapper_arguments": [
            {"arg0": f"{args[0]:08x}", "arg1": f"{args[1]:08x}", "arg2": f"{args[2]:08x}", "calls": calls}
            for args, calls in sorted(requests.items())
        ],
        "records": records,
        "limitations": [
            "Durations are inclusive host wall time for selected recompiled functions, not GPU time or proof of a loading boundary.",
            "Small collector/timestamp overhead remains; persistent log flushing is deferred until the outermost selected call exits.",
            "ecx/caller/arg0-arg2 are raw entry dwords; only a separately validated function signature may assign them meaning.",
            "The 0x00838b40 wrapper can return early; its completed records do not prove that a GameStateRequest was enqueued.",
            "Active calls at a crash or abnormal exit have no completion record; absence is not a zero-duration measurement.",
            "These records do not establish device FPS, visual correctness, or a warm-cache drain benefit.",
        ],
    }


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=Path)
    parser.add_argument("--functions", type=Path, default=root / "re/functions.csv")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        report = analyze(args.logs, args.functions)
        encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if args.output:
            args.output.write_text(encoded)
        else:
            print(encoded, end="")
    except (OSError, UnicodeError, ValueError, KeyError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
