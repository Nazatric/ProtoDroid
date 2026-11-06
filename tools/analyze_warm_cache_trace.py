#!/usr/bin/env python3
"""Validate warm-cache snapshots and correlate them with guest-function completions in the same log."""
from __future__ import annotations

import argparse
from bisect import bisect_left
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re
import sys

from analyze_guest_function_trace import parse_record as parse_guest_record, tokens

WARM_MARKER = re.compile(r"DXVK warm-cache snapshot:(?P<body>(?: [^\r\n]*)?)$")
WARM_FIELDS = {
    "at_ns", "state_known", "state_pending", "pipeline_total", "pipeline_completed", "pipeline_pending",
}
MAX_UINT64 = 0xffffffffffffffff


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def uint(token: str, field: str, source: Path, line_number: int) -> int:
    try:
        value = int(token, 10)
    except ValueError as error:
        raise ValueError(f"{source}:{line_number}: invalid {field}") from error
    if not 0 <= value <= MAX_UINT64:
        raise ValueError(f"{source}:{line_number}: out-of-range {field}")
    return value


def parse_warm_snapshot(line: str, source: Path, line_number: int, input_index: int) -> dict | None:
    if "DXVK warm-cache snapshot:" not in line:
        return None
    match = WARM_MARKER.search(line)
    if match is None:
        raise ValueError(f"{source}:{line_number}: malformed warm-cache snapshot")
    fields = tokens(match.group("body"), source, line_number)
    if set(fields) != WARM_FIELDS:
        raise ValueError(f"{source}:{line_number}: wrong warm-cache snapshot fields")
    values = {field: uint(fields[field], field, source, line_number) for field in sorted(WARM_FIELDS)}
    if values["state_known"] not in (0, 1):
        raise ValueError(f"{source}:{line_number}: invalid state_known")
    total = values["pipeline_total"]
    completed = values["pipeline_completed"]
    pending = max(total - completed, 0)
    if values["pipeline_pending"] != pending:
        raise ValueError(f"{source}:{line_number}: pipeline_pending/raw counter mismatch")
    known = bool(values["state_known"])
    ordered = completed <= total
    queue_idle = None if not known or not ordered else values["state_pending"] == 0 and pending == 0
    return {
        "input_index": input_index,
        "source": str(source),
        "line": line_number,
        "at_ns": values["at_ns"],
        "state_known": known,
        "state_pending_raw": values["state_pending"],
        "state_pending": values["state_pending"] if known else None,
        "pipeline_total": total,
        "pipeline_completed": completed,
        "pipeline_pending": pending,
        "counter_order": "ok" if ordered else "completed_exceeds_total",
        "queue_idle": queue_idle,
    }


def snapshot_ref(snapshot: dict, end_ns: int, before: bool) -> dict:
    return {
        "snapshot_id": snapshot["snapshot_id"],
        "delta_ns": end_ns - snapshot["at_ns"] if before else snapshot["at_ns"] - end_ns,
    }


def analyze(paths: list[Path], correlation_window_ms: int = 500,
            paired_inputs: list[tuple[int, int]] | None = None) -> dict:
    if not 0 <= correlation_window_ms <= 60000:
        raise ValueError("correlation window must be between 0 and 60000 ms")
    window_ns = correlation_window_ms * 1000000
    paired_inputs = paired_inputs or []
    groups: list[list[int]] = []
    input_group: dict[int, int] = {}
    for first, second in paired_inputs:
        if first == second or first in input_group or second in input_group or \
                not (0 <= first < len(paths) and 0 <= second < len(paths)):
            raise ValueError("explicit correlation pairs must use distinct, unpaired input indices")
        input_group[first] = input_group[second] = len(groups)
        groups.append([first, second])
    for input_index in range(len(paths)):
        if input_index not in input_group:
            input_group[input_index] = len(groups)
            groups.append([input_index])

    inputs = []
    snapshots_by_input: dict[int, list[dict]] = defaultdict(list)
    records_by_input: dict[int, list[dict]] = defaultdict(list)
    for input_index, path in enumerate(paths):
        data = path.read_bytes()
        info = {
            "path": str(path), "bytes": len(data), "sha256": sha256(data),
            "guest_record_count": 0, "warm_cache_snapshot_count": 0,
        }
        seen_sequences: set[int] = set()
        for line_number, line in enumerate(data.decode("utf-8").splitlines(), 1):
            snapshot = parse_warm_snapshot(line, path, line_number, input_index)
            if snapshot is not None:
                snapshots_by_input[input_index].append(snapshot)
                info["warm_cache_snapshot_count"] += 1
                continue
            record = parse_guest_record(line, path, line_number, seen_sequences)
            if record is not None:
                record["input_index"] = input_index
                records_by_input[input_index].append(record)
                info["guest_record_count"] += 1
        inputs.append(info)
    if not any(snapshots_by_input.values()):
        raise ValueError("no DXVK warm-cache snapshots found")

    snapshots = []
    correlations = []
    uncorrelated = 0
    clock_regression_excluded = 0
    guest_anomalies = Counter()
    for input_index in range(len(paths)):
        current = sorted(snapshots_by_input[input_index],
                         key=lambda item: (item["at_ns"], item["line"]))
        for snapshot_index, snapshot in enumerate(current):
            snapshot["snapshot_index"] = snapshot_index
            snapshot["snapshot_id"] = f"{input_index}:{snapshot_index}"
            snapshots.append(snapshot)

    for group_index, group in enumerate(groups):
        current = sorted((snapshot for input_index in group for snapshot in snapshots_by_input[input_index]),
                         key=lambda item: (item["at_ns"], item["input_index"], item["line"]))
        times = [snapshot["at_ns"] for snapshot in current]
        for input_index in group:
            for record in records_by_input[input_index]:
                kind = record["event"]
                if kind != "complete":
                    guest_anomalies[kind] += 1
                    continue
                for field in ("clock_regression", "discarded_inner"):
                    if record[field]:
                        guest_anomalies[field] += 1
                if record["clock_regression"]:
                    clock_regression_excluded += 1
                    continue
                end_ns = record["end_ns"]
                position = bisect_left(times, end_ns)
                before = current[position - 1] if position > 0 else None
                after = current[position] if position < len(current) else None
                if before is not None and end_ns - before["at_ns"] > window_ns:
                    before = None
                if after is not None and after["at_ns"] - end_ns > window_ns:
                    after = None
                if before is None and after is None:
                    uncorrelated += 1
                    continue
                correlations.append({
                    "correlation_group": group_index,
                    "input_index": input_index,
                    "guest_sequence": record["sequence"],
                    "guest_source_line": record["line"],
                    "rva": f'{record["rva"]:08x}',
                    "start_ns": record["start_ns"],
                    "end_ns": end_ns,
                    "duration_ns": record["duration_ns"],
                    "before": snapshot_ref(before, end_ns, True) if before is not None else None,
                    "after": snapshot_ref(after, end_ns, False) if after is not None else None,
                })

    known_pending = [s["state_pending"] for s in snapshots if s["state_known"]]
    idle_counts = Counter("unknown" if s["queue_idle"] is None else str(s["queue_idle"]).lower()
                          for s in snapshots)
    complete_count = sum(1 for records in records_by_input.values() for r in records if r["event"] == "complete")
    return {
        "schema_version": 1,
        "status": "valid-warm-cache-snapshots",
        "input_files": inputs,
        "correlation_groups": [{"input_indices": group} for group in groups],
        "correlation_window_ns": window_ns,
        "snapshot_count": len(snapshots),
        "state_unknown_count": sum(not s["state_known"] for s in snapshots),
        "counter_order_inversion_count": sum(s["counter_order"] != "ok" for s in snapshots),
        "queue_idle_counts": dict(sorted(idle_counts.items())),
        "max_known_state_pending": max(known_pending) if known_pending else None,
        "max_pipeline_pending": max(s["pipeline_pending"] for s in snapshots),
        "guest_record_count": sum(info["guest_record_count"] for info in inputs),
        "guest_complete_count": complete_count,
        "guest_anomaly_counts": dict(sorted(guest_anomalies.items())),
        "correlated_complete_count": len(correlations),
        "uncorrelated_complete_count": uncorrelated,
        "clock_regression_excluded_count": clock_regression_excluded,
        "warm_cache_snapshots": snapshots,
        "queue_at_guest_function_completion": correlations,
        "limitations": [
            "Snapshots are instantaneous Present-driven samples (nominally 250 ms apart), not continuous queue-lifetime history.",
            "No Present calls means no snapshots; a no-Present stall cannot be reconstructed from this diagnostic.",
            "Guest completions with clock_regression=1 are counted as anomalies and excluded from temporal correlation.",
            "state_pending is null when try-lock contention makes the feeder count unknown; its raw numeric field is preserved separately.",
            "Pipeline counters are relaxed, nontransactional samples; completed greater than total is retained and makes queue_idle null.",
            "Correlations never cross correlation groups. Standalone inputs are isolated; explicit pairs assert same-run provenance.",
            "The tool cannot independently prove that an explicit pair came from one run; never pair unrelated captures.",
            "Temporal brackets do not prove loading semantics, causation, drain benefit, or GPU time.",
            "No warm-cache drain is invoked by this diagnostic, and these data are not device FPS or visual-correctness evidence.",
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="*", type=Path,
                        help="standalone logs; same-file records correlate, separate inputs stay isolated")
    parser.add_argument("--pair", nargs=2, action="append", type=Path, default=[],
                        metavar=("GUEST_LOG", "DXVK_LOG"),
                        help="explicitly correlate two logs asserted to be from the same run; repeatable")
    parser.add_argument("--correlation-window-ms", type=int, default=500)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    paths = list(args.logs)
    paired_inputs = []
    for pair in args.pair:
        first = len(paths)
        paths.extend(pair)
        paired_inputs.append((first, first + 1))
    if not paths:
        parser.error("provide standalone logs or at least one --pair")
    try:
        report = analyze(paths, args.correlation_window_ms, paired_inputs)
        encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if args.output:
            args.output.write_text(encoded, encoding="utf-8")
        else:
            print(encoded, end="")
    except (OSError, UnicodeError, ValueError, KeyError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
