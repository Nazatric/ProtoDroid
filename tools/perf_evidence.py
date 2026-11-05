#!/usr/bin/env python3
"""Extract and compare ProtoDroid performance evidence without inventing aggregate device results."""

from __future__ import annotations

import argparse
import json
import math
import re
from pathlib import Path
from typing import Any

TOKEN = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=([^\s;]+)")
NUMBER = re.compile(r"^(-?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+))(?:ms|s|%)?$")
MONO = re.compile(r"^\[mono=([0-9.]+) tid=(\d+)\]")
PHASE = re.compile(r"phase=(.*?) index=(\d+) status=([^ ]+)(?: |$)")
COMPARE_FIELDS = (
    "phase_present_average_ms",
    "phase_present_max_ms",
    "present_window_fps",
    "present_window_average_ms",
    "present_window_p99_ms",
    "present_window_max_ms",
    "process_cpu_utilization_pct",
    "wall_ms",
)


def number(value: str) -> float | None:
    match = NUMBER.match(value)
    if not match:
        return None
    result = float(match.group(1))
    return result if math.isfinite(result) else None


def tokens(text: str) -> dict[str, str]:
    return {key: value for key, value in TOKEN.findall(text)}


def numeric_tokens(text: str) -> dict[str, float]:
    result = {}
    for key, value in tokens(text).items():
        parsed = number(value)
        if parsed is not None:
            result[key] = parsed
    return result


def parse_logs(paths: list[str | Path]) -> dict[str, Any]:
    result: dict[str, Any] = {
        "format": 1,
        "files": [str(path) for path in paths],
        "source": None,
        "device": {},
        "gpu": None,
        "config": {},
        "present_windows": [],
        "phases": [],
    }
    for path in paths:
        for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
            source = re.search(r"(?:protodroid source |source=)([0-9a-f]{40})", line)
            if source:
                if result["source"] not in (None, source.group(1)):
                    raise ValueError(f"multiple source revisions in one evidence set: {source.group(1)}")
                result["source"] = source.group(1)
            if "device: model=" in line:
                result["device"] = tokens(line.split("device: ", 1)[1])
            if "vulkan: system driver sees " in line:
                gpu = line.split("vulkan: system driver sees ", 1)[1].split(" (profile", 1)[0]
                result["gpu"] = gpu
            if "config: " in line and " = " in line:
                key, value = line.split("config: ", 1)[1].split(" = ", 1)
                result["config"][key] = value
            if "diag: Present wall " in line:
                metrics = numeric_tokens(line)
                required = {"n", "avg", "p50", "p95", "p99", "max"}
                if required <= metrics.keys():
                    mono = MONO.match(line)
                    result["present_windows"].append({
                        "at_s": float(mono.group(1)) if mono else None,
                        "tid": int(mono.group(2)) if mono else None,
                        **{key: metrics[key] for key in sorted(required)},
                    })
            match = PHASE.search(line)
            if match and match.group(3) == "tested":
                result["phases"].append({
                    "name": match.group(1),
                    "index": int(match.group(2)),
                    "metrics": numeric_tokens(line[match.end():]),
                })
    result["phases"].sort(key=lambda phase: (phase["index"], phase["name"]))
    return result


def compare_summaries(baseline: dict[str, Any], candidate: dict[str, Any]) -> dict[str, Any]:
    warnings: list[str] = []
    comparable = True
    if baseline.get("device") != candidate.get("device"):
        comparable = False
        warnings.append("device metadata differs; these runs are not a controlled same-device comparison")
    if baseline.get("gpu") != candidate.get("gpu"):
        comparable = False
        warnings.append("GPU metadata differs")
    if baseline.get("config") != candidate.get("config"):
        comparable = False
        warnings.append("captured game configuration differs")
    before = {(p["index"], p["name"]): p for p in baseline.get("phases", [])}
    after = {(p["index"], p["name"]): p for p in candidate.get("phases", [])}
    if set(before) != set(after):
        comparable = False
        warnings.append("tested phase identity differs")
    phases = []
    compared_metric_count = 0
    for identity in sorted(set(before) & set(after)):
        b, a = before[identity], after[identity]
        metrics = []
        for field in COMPARE_FIELDS:
            if field not in b["metrics"] or field not in a["metrics"]:
                continue
            old, new = b["metrics"][field], a["metrics"][field]
            metrics.append({
                "field": field,
                "baseline": old,
                "candidate": new,
                "delta_pct": (new - old) * 100.0 / old if old else None,
                "direction": "higher-is-better" if field == "present_window_fps" else "lower-is-better",
            })
            compared_metric_count += 1
        phases.append({"index": identity[0], "name": identity[1], "metrics": metrics})
    if not phases or compared_metric_count == 0:
        comparable = False
        warnings.append("no common tested phases with performance metrics")
    return {
        "format": 1,
        "baseline_source": baseline.get("source"),
        "candidate_source": candidate.get("source"),
        "comparable": comparable,
        "warnings": warnings,
        "phases": phases,
        "note": "Evidence comparison only; no visual, device-wide, or adoption conclusion is automatic.",
    }


def validate_raw_report(report: dict[str, Any], label: str) -> None:
    if report.get("format") != "protodroid_frame_intervals_v1":
        raise ValueError(f"{label}: unsupported raw frame-report format")
    if not isinstance(report.get("sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", report["sha256"]):
        raise ValueError(f"{label}: missing raw input SHA-256")
    rows = report.get("rows")
    total_ns = report.get("total_interval_ns")
    if isinstance(rows, bool) or not isinstance(rows, int) or rows <= 0 or \
            isinstance(total_ns, bool) or not isinstance(total_ns, int) or total_ns <= 0:
        raise ValueError(f"{label}: invalid row/total counts")
    for field in ("average_interval_ms", "average_present_rate_per_second", "max_interval_ms"):
        value = report.get(field)
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
            raise ValueError(f"{label}: invalid {field}")
    expected_average = total_ns / rows / 1.0e6
    if not math.isclose(report["average_interval_ms"], expected_average, rel_tol=1.0e-12, abs_tol=1.0e-12):
        raise ValueError(f"{label}: average does not match total_interval_ns/rows")
    percentiles = report.get("nearest_rank_percentiles_ms")
    if not isinstance(percentiles, dict) or set(percentiles) != {"p50", "p95", "p99"}:
        raise ValueError(f"{label}: missing exact global percentiles")
    values = [percentiles[key] for key in ("p50", "p95", "p99")]
    if any(isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0
           for value in values):
        raise ValueError(f"{label}: invalid percentile")
    if not (values[0] <= values[1] <= values[2] <= report["max_interval_ms"]):
        raise ValueError(f"{label}: non-monotonic percentile/max values")
    thresholds = report.get("strict_threshold_counts")
    if not isinstance(thresholds, dict) or not thresholds:
        raise ValueError(f"{label}: missing strict threshold counts")
    previous = rows
    for threshold in sorted(thresholds, key=lambda value: float(value)):
        count = thresholds[threshold]
        if isinstance(count, bool) or not isinstance(count, int) or count < 0 or count > previous:
            raise ValueError(f"{label}: invalid/non-monotonic threshold counts")
        previous = count
    if not isinstance(report.get("closed"), bool):
        raise ValueError(f"{label}: invalid close status")


def raw_delta(baseline: float, candidate: float, direction: str) -> dict[str, Any]:
    return {"baseline": baseline, "candidate": candidate, "delta": candidate - baseline,
            "delta_pct": (candidate - baseline) * 100.0 / baseline if baseline else None,
            "direction": direction}


def compare_raw_reports(baseline: dict[str, Any], candidate: dict[str, Any],
                        baseline_raw: dict[str, Any], candidate_raw: dict[str, Any],
                        baseline_protocol: dict[str, str], candidate_protocol: dict[str, str]) -> dict[str, Any]:
    validate_raw_report(baseline_raw, "baseline")
    validate_raw_report(candidate_raw, "candidate")
    metadata = compare_summaries(baseline, candidate)
    # Raw reports supply the performance metrics. Phase identity still matters when phases exist, but old-style
    # per-window metric coverage is not required for an exact raw comparison (including ordinary-play captures).
    warnings = [warning for warning in metadata["warnings"] if warning != "no common tested phases with performance metrics"]
    for field, label in (("source", "source revision"), ("device", "device metadata"), ("gpu", "GPU metadata"),
                         ("config", "game configuration")):
        if not baseline.get(field) or not candidate.get(field):
            warnings.append(f"missing {label} prevents a controlled raw comparison")
    if baseline_protocol.get("scene") != candidate_protocol.get("scene") or not baseline_protocol.get("scene"):
        warnings.append("scene/protocol labels differ or are empty")
    if baseline_protocol.get("cache_state") != candidate_protocol.get("cache_state") or \
            not baseline_protocol.get("cache_state"):
        warnings.append("cache-state labels differ or are empty")
    for label, raw in (("baseline", baseline_raw), ("candidate", candidate_raw)):
        if not raw["closed"]:
            warnings.append(f"{label} raw capture is a valid prefix without a close trailer")
    baseline_thresholds = baseline_raw["strict_threshold_counts"]
    candidate_thresholds = candidate_raw["strict_threshold_counts"]
    if set(baseline_thresholds) != set(candidate_thresholds):
        warnings.append("raw threshold identities differ")
    metrics = {
        "average_interval_ms": raw_delta(baseline_raw["average_interval_ms"], candidate_raw["average_interval_ms"],
                                          "lower-is-better"),
        "average_present_rate_per_second": raw_delta(baseline_raw["average_present_rate_per_second"],
                                                      candidate_raw["average_present_rate_per_second"],
                                                      "higher-is-better"),
        "max_interval_ms": raw_delta(baseline_raw["max_interval_ms"], candidate_raw["max_interval_ms"],
                                      "lower-is-better"),
    }
    for percentile in ("p50", "p95", "p99"):
        metrics[percentile + "_ms"] = raw_delta(baseline_raw["nearest_rank_percentiles_ms"][percentile],
                                                candidate_raw["nearest_rank_percentiles_ms"][percentile],
                                                "lower-is-better")
    thresholds = {}
    for threshold in sorted(set(baseline_thresholds) & set(candidate_thresholds), key=lambda value: float(value)):
        old_count, new_count = baseline_thresholds[threshold], candidate_thresholds[threshold]
        old_rate = old_count * 1000.0 / baseline_raw["rows"]
        new_rate = new_count * 1000.0 / candidate_raw["rows"]
        thresholds[threshold] = {"baseline_count": old_count, "candidate_count": new_count,
                                 "baseline_per_1000": old_rate, "candidate_per_1000": new_rate,
                                 "delta_per_1000": new_rate - old_rate, "direction": "lower-is-better"}
    notes = []
    if baseline_raw["rows"] != candidate_raw["rows"]:
        notes.append("row counts differ; threshold counts are compared as rates per 1000 completed intervals")
    return {
        "format": 1,
        "baseline_source": baseline.get("source"),
        "candidate_source": candidate.get("source"),
        "comparable": not warnings,
        "warnings": warnings,
        "notes": notes,
        "protocol": {"baseline": baseline_protocol, "candidate": candidate_protocol,
                     "status": "caller-declared metadata, not independently verified scene/cache proof"},
        "coverage": {"baseline": {"rows": baseline_raw["rows"], "total_interval_ns": baseline_raw["total_interval_ns"],
                                  "sha256": baseline_raw["sha256"], "completeness": baseline_raw.get("completeness")},
                     "candidate": {"rows": candidate_raw["rows"], "total_interval_ns": candidate_raw["total_interval_ns"],
                                   "sha256": candidate_raw["sha256"], "completeness": candidate_raw.get("completeness")}},
        "metrics": metrics,
        "strict_thresholds_ms": thresholds,
        "note": "Exact completed-Present A/B evidence only; no significance, visual correctness, GPU-time, or adoption conclusion is automatic.",
    }


def write_or_print(value: dict[str, Any], output: str | None) -> None:
    encoded = json.dumps(value, indent=2, sort_keys=True) + "\n"
    if output:
        Path(output).write_text(encoded, encoding="utf-8")
        print(f"Wrote {output}")
    else:
        print(encoded, end="")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    summarize = commands.add_parser("summarize")
    summarize.add_argument("logs", nargs="+")
    summarize.add_argument("--output")
    compare = commands.add_parser("compare")
    compare.add_argument("baseline_json")
    compare.add_argument("candidate_json")
    compare.add_argument("--output")
    raw = commands.add_parser("compare-raw")
    raw.add_argument("baseline_summary")
    raw.add_argument("candidate_summary")
    raw.add_argument("baseline_raw")
    raw.add_argument("candidate_raw")
    for side in ("baseline", "candidate"):
        raw.add_argument(f"--{side}-scene", required=True)
        raw.add_argument(f"--{side}-cache-state", required=True)
    raw.add_argument("--output")
    args = parser.parse_args()
    if args.command == "summarize":
        write_or_print(parse_logs(args.logs), args.output)
    elif args.command == "compare":
        baseline = json.loads(Path(args.baseline_json).read_text(encoding="utf-8"))
        candidate = json.loads(Path(args.candidate_json).read_text(encoding="utf-8"))
        write_or_print(compare_summaries(baseline, candidate), args.output)
    else:
        baseline = json.loads(Path(args.baseline_summary).read_text(encoding="utf-8"))
        candidate = json.loads(Path(args.candidate_summary).read_text(encoding="utf-8"))
        baseline_raw = json.loads(Path(args.baseline_raw).read_text(encoding="utf-8"))
        candidate_raw = json.loads(Path(args.candidate_raw).read_text(encoding="utf-8"))
        baseline_protocol = {"scene": args.baseline_scene, "cache_state": args.baseline_cache_state}
        candidate_protocol = {"scene": args.candidate_scene, "cache_state": args.candidate_cache_state}
        write_or_print(compare_raw_reports(baseline, candidate, baseline_raw, candidate_raw,
                                           baseline_protocol, candidate_protocol), args.output)


if __name__ == "__main__":
    main()
