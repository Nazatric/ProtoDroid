#!/usr/bin/env python3
"""Analyze ProtoDroid's raw Present-to-Present CSV without window aggregation.

The input contains completed D3D9 Present intervals, not GPU times or physical-display
present/drop events. A missing close trailer is reported as an unclosed valid prefix,
never silently promoted to a complete run.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Iterable

VERSION = "protodroid_frame_intervals_v1"
THRESHOLDS_MS = (8.5, 17, 34, 50, 100, 250, 1000)


class TraceError(ValueError):
    pass


def percentile_ns(sorted_values: list[int], percent: int) -> int:
    if not sorted_values:
        return 0
    rank = max(1, min(len(sorted_values), (percent * len(sorted_values) + 99) // 100))
    return sorted_values[rank - 1]


def analyze_lines(lines: Iterable[str], source: str = "<memory>", digest: str | None = None) -> dict:
    intervals: list[int] = []
    ends: list[int] = []
    previous_end: int | None = None
    saw_version = False
    saw_columns = False
    closed_reason: str | None = None
    trailer_rows: int | None = None

    for line_number, original in enumerate(lines, 1):
        line = original.strip()
        if not line:
            continue
        if line.startswith("#"):
            if VERSION in line:
                saw_version = True
            if line.startswith("# closed="):
                if closed_reason is not None:
                    raise TraceError(f"{source}:{line_number}: duplicate close trailer")
                payload = line[len("# closed="):]
                parts = payload.split()
                if not parts or not parts[0]:
                    raise TraceError(f"{source}:{line_number}: empty close reason")
                closed_reason = parts[0]
                row_fields = [part for part in parts[1:] if part.startswith("rows=")]
                if len(row_fields) != 1:
                    raise TraceError(f"{source}:{line_number}: close trailer lacks one rows field")
                try:
                    trailer_rows = int(row_fields[0][5:])
                except ValueError as error:
                    raise TraceError(f"{source}:{line_number}: invalid trailer row count") from error
            continue
        if line == "sequence,present_end_ns,interval_ns":
            saw_columns = True
            continue
        if not saw_version or not saw_columns:
            raise TraceError(f"{source}:{line_number}: data before version/column header")
        if closed_reason is not None:
            raise TraceError(f"{source}:{line_number}: data after close trailer")
        fields = line.split(",")
        if len(fields) != 3:
            raise TraceError(f"{source}:{line_number}: expected three CSV fields")
        try:
            sequence, end_ns, interval_ns = (int(field) for field in fields)
        except ValueError as error:
            raise TraceError(f"{source}:{line_number}: non-integer CSV field") from error
        if sequence != len(intervals):
            raise TraceError(f"{source}:{line_number}: sequence {sequence}, expected {len(intervals)}")
        if interval_ns <= 0 or end_ns < 0:
            raise TraceError(f"{source}:{line_number}: non-positive interval or negative timestamp")
        if previous_end is not None and end_ns - previous_end != interval_ns:
            raise TraceError(f"{source}:{line_number}: Present interval is not contiguous with the previous row")
        intervals.append(interval_ns)
        ends.append(end_ns)
        previous_end = end_ns

    if not saw_version or not saw_columns:
        raise TraceError(f"{source}: missing {VERSION} version or column header")
    if not intervals:
        raise TraceError(f"{source}: no completed Present intervals")
    if closed_reason is not None and trailer_rows != len(intervals):
        raise TraceError(f"{source}: trailer rows={trailer_rows}, actual rows={len(intervals)}")

    ordered = sorted(intervals)
    total_ns = sum(intervals)
    thresholds = {
        str(threshold).rstrip("0").rstrip(".") if isinstance(threshold, float) else str(threshold):
        sum(interval > int(threshold * 1_000_000) for interval in intervals)
        for threshold in THRESHOLDS_MS
    }
    return {
        "format": VERSION,
        "source": source,
        "sha256": digest,
        "rows": len(intervals),
        "first_present_end_ns": ends[0],
        "last_present_end_ns": ends[-1],
        "total_interval_ns": total_ns,
        "average_interval_ms": total_ns / len(intervals) / 1e6,
        "average_present_rate_per_second": len(intervals) * 1e9 / total_ns,
        "nearest_rank_percentiles_ms": {
            f"p{percent}": percentile_ns(ordered, percent) / 1e6 for percent in (50, 95, 99)
        },
        "max_interval_ms": ordered[-1] / 1e6,
        "strict_threshold_counts": thresholds,
        "closed": closed_reason is not None,
        "close_reason": closed_reason,
        "completeness": "closed" if closed_reason is not None else "valid-prefix-without-close-trailer",
        "semantics": "completed D3D9 Present-to-Present intervals; not GPU time, physical display drops, or proof of visual completeness",
    }


def analyze_file(path: Path) -> dict:
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    with path.open("r", encoding="utf-8", newline="") as handle:
        return analyze_lines(handle, str(path), digest)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = analyze_file(args.csv)
    text = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    print(text, end="")


if __name__ == "__main__":
    main()
