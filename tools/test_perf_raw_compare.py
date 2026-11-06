#!/usr/bin/env python3
"""Matched exact raw-frame A/B comparison tests, including comparability failures."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

PERF = load("perf_evidence_raw", ROOT / "tools/perf_evidence.py")
FRAMES = load("frame_analysis_for_raw_compare", ROOT / "tools/analyze_frame_intervals.py")
A, B = "a" * 40, "b" * 40
PROTOCOL = {"scene": "static-outdoor", "cache_state": "warm"}


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def csv_text(intervals, closed=True):
    lines = ["# protodroid_frame_intervals_v1; test\n", "sequence,present_end_ns,interval_ns\n"]
    end = 1000000000
    for sequence, milliseconds in enumerate(intervals):
        duration = milliseconds * 1000000
        end += duration
        lines.append(f"{sequence},{end},{duration}\n")
    if closed:
        lines.append(f"# closed=final rows={len(intervals)}\n")
    return "".join(lines)


def summary_text(source=A, model="Phone"):
    return (f"protodroid source {source}\n"
            f"device: model={model} device=phone soc=SOC platform=plat hardware=plat android=16 sdk=36 abi=arm64-v8a\n"
            "vulkan: system driver sees Test GPU (profile test, device Vulkan 1.3.1)\n"
            "config: game_aa_level = -1\n")


with tempfile.TemporaryDirectory(prefix="raw-frame-ab-test-") as temp_name:
    temp = Path(temp_name)
    baseline_csv, candidate_csv = temp / "baseline.csv", temp / "candidate.csv"
    baseline_csv.write_text(csv_text([20] * 10))
    candidate_csv.write_text(csv_text([10] * 9 + [100]))
    baseline_raw, candidate_raw = FRAMES.analyze_file(baseline_csv), FRAMES.analyze_file(candidate_csv)
    baseline_log, candidate_log = temp / "baseline.log", temp / "candidate.log"
    baseline_log.write_text(summary_text(A))
    candidate_log.write_text(summary_text(B))
    baseline, candidate = PERF.parse_logs([baseline_log]), PERF.parse_logs([candidate_log])
    result = PERF.compare_raw_reports(baseline, candidate, baseline_raw, candidate_raw, PROTOCOL, PROTOCOL)
    check(result["comparable"] and not result["warnings"], "ordinary-play raw comparison without phases")
    check(result["metrics"]["average_interval_ms"]["delta_pct"] == -5.0, "average interval delta")
    check(abs(result["metrics"]["average_present_rate_per_second"]["delta_pct"] - 100.0 / 19.0) < 1e-9,
          "present-rate delta")
    check(result["metrics"]["p95_ms"]["baseline"] == 20.0 and
          result["metrics"]["p95_ms"]["candidate"] == 100.0 and
          result["metrics"]["p95_ms"]["delta_pct"] == 400.0, "exact global p95 comparison")
    check(result["metrics"]["p50_ms"]["delta_pct"] == -50.0 and result["metrics"]["max_interval_ms"]["delta"] == 80.0,
          "p50/max metrics")
    check(result["strict_thresholds_ms"]["34"]["candidate_per_1000"] == 100.0 and
          result["strict_thresholds_ms"]["34"]["delta_per_1000"] == 100.0, "normalized strict thresholds")
    check("no significance" in result["note"] and "visual correctness" in result["note"], "non-adoption limitation")
    aa = PERF.compare_raw_reports(baseline, baseline, baseline_raw, baseline_raw, PROTOCOL, PROTOCOL)
    check(aa["comparable"] and aa["metrics"]["p99_ms"]["delta"] == 0, "A/A control")

    cold = dict(PROTOCOL, cache_state="cold")
    check(not PERF.compare_raw_reports(baseline, candidate, baseline_raw, candidate_raw, PROTOCOL, cold)["comparable"],
          "cold/warm mismatch")
    other_scene = dict(PROTOCOL, scene="other")
    check(not PERF.compare_raw_reports(baseline, candidate, baseline_raw, candidate_raw, PROTOCOL, other_scene)["comparable"],
          "scene mismatch")
    other_device = PERF.parse_logs([candidate_log])
    other_device["device"]["model"] = "Other"
    check(not PERF.compare_raw_reports(baseline, other_device, baseline_raw, candidate_raw, PROTOCOL, PROTOCOL)["comparable"],
          "device mismatch")
    missing_source = dict(candidate, source=None)
    check(not PERF.compare_raw_reports(baseline, missing_source, baseline_raw, candidate_raw, PROTOCOL, PROTOCOL)["comparable"],
          "missing exact source")

    unclosed_csv = temp / "unclosed.csv"
    unclosed_csv.write_text(csv_text([10] * 9 + [100], closed=False))
    unclosed = FRAMES.analyze_file(unclosed_csv)
    partial = PERF.compare_raw_reports(baseline, candidate, baseline_raw, unclosed, PROTOCOL, PROTOCOL)
    check(not partial["comparable"] and any("without a close trailer" in warning for warning in partial["warnings"]),
          "unclosed candidate rejection")

    malformed = []
    bad = dict(baseline_raw, average_interval_ms=1.0); malformed.append(bad)
    bad = dict(baseline_raw, nearest_rank_percentiles_ms={"p50": 20.0, "p95": 19.0, "p99": 20.0}); malformed.append(bad)
    bad = dict(baseline_raw, strict_threshold_counts={"8.5": 11}); malformed.append(bad)
    bad = dict(baseline_raw, sha256="bad"); malformed.append(bad)
    bad = dict(baseline_raw, format="other"); malformed.append(bad)
    for index, report in enumerate(malformed):
        try:
            PERF.compare_raw_reports(baseline, candidate, report, candidate_raw, PROTOCOL, PROTOCOL)
        except ValueError:
            pass
        else:
            raise AssertionError(f"malformed raw report accepted: {index}")

    paths = {}
    for name, value in (("baseline-summary", baseline), ("candidate-summary", candidate),
                        ("baseline-raw", baseline_raw), ("candidate-raw", candidate_raw)):
        path = temp / (name + ".json")
        path.write_text(json.dumps(value))
        paths[name] = path
    command = [sys.executable, str(ROOT / "tools/perf_evidence.py"), "compare-raw",
               str(paths["baseline-summary"]), str(paths["candidate-summary"]),
               str(paths["baseline-raw"]), str(paths["candidate-raw"]),
               "--baseline-scene", PROTOCOL["scene"], "--candidate-scene", PROTOCOL["scene"],
               "--baseline-cache-state", "warm", "--candidate-cache-state", "warm"]
    cli = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
    check(cli.returncode == 0 and json.loads(cli.stdout) == result, "compare-raw CLI")
print("Raw frame A/B comparator tests passed (exact global metrics, A/A, protocol/metadata gates, 5 malformed reports, CLI)")
