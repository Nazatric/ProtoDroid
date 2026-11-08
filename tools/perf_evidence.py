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
    args = parser.parse_args()
    if args.command == "summarize":
        write_or_print(parse_logs(args.logs), args.output)
    else:
        baseline = json.loads(Path(args.baseline_json).read_text(encoding="utf-8"))
        candidate = json.loads(Path(args.candidate_json).read_text(encoding="utf-8"))
        write_or_print(compare_summaries(baseline, candidate), args.output)


if __name__ == "__main__":
    main()
