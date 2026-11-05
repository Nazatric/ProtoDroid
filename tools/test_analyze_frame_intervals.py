#!/usr/bin/env python3
from pathlib import Path
import hashlib
import importlib.util
import json
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("analyze_frame_intervals", ROOT / "tools/analyze_frame_intervals.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def lines(intervals=(16, 17, 18, 34, 100), close="final"):
    output = ["# protodroid_frame_intervals_v1; test\n", "sequence,present_end_ns,interval_ns\n"]
    end = 1_000_000_000
    for sequence, milliseconds in enumerate(intervals):
        duration = int(milliseconds * 1_000_000)
        end += duration
        output.append(f"{sequence},{end},{duration}\n")
    if close is not None:
        output.append(f"# closed={close} rows={len(intervals)}\n")
    return output


result = MODULE.analyze_lines(lines(), "fixture")
assert result["rows"] == 5
assert result["total_interval_ns"] == 185_000_000
assert result["average_interval_ms"] == 37.0
assert abs(result["average_present_rate_per_second"] - 27.027027027027) < 1e-9
assert result["nearest_rank_percentiles_ms"] == {"p50": 18.0, "p95": 100.0, "p99": 100.0}
assert result["max_interval_ms"] == 100.0
assert result["strict_threshold_counts"] == {"8.5": 5, "17": 3, "34": 1, "50": 1,
                                              "100": 0, "250": 0, "1000": 0}
assert result["closed"] is True and result["close_reason"] == "final"
assert result["completeness"] == "closed"
assert "not GPU time" in result["semantics"]
unclosed = MODULE.analyze_lines(lines(close=None), "prefix")
assert unclosed["closed"] is False and unclosed["completeness"] == "valid-prefix-without-close-trailer"


def rejected(name, fixture):
    try:
        MODULE.analyze_lines(fixture, name)
    except MODULE.TraceError:
        return
    raise AssertionError(f"invalid trace was accepted: {name}")


gap = lines()
gap[3] = "9" + gap[3][1:]
rejected("sequence gap", gap)
discontinuous = lines()
fields = discontinuous[3].strip().split(",")
fields[1] = str(int(fields[1]) + 1)
discontinuous[3] = ",".join(fields) + "\n"
rejected("discontinuous Present timestamps", discontinuous)
rejected("zero interval", lines((16, 0, 18)))
rejected("trailer count mismatch", lines()[:-1] + ["# closed=final rows=99\n"])
rejected("duplicate close", lines() + ["# closed=again rows=5\n"])
rejected("data after close", lines() + ["5,9999999999,1000\n"])
rejected("missing header", lines()[1:])
rejected("no samples", lines((), close=None))

with tempfile.TemporaryDirectory(prefix="protodroid-frame-analysis-") as temp:
    path = Path(temp) / "frames.csv"
    path.write_text("".join(lines()), encoding="utf-8")
    disk = MODULE.analyze_file(path)
    assert disk["sha256"] == hashlib.sha256(path.read_bytes()).hexdigest()
    assert disk["rows"] == 5
    output = Path(temp) / "analysis.json"
    output.write_text(json.dumps(disk), encoding="utf-8")
    assert json.loads(output.read_text())["nearest_rank_percentiles_ms"]["p95"] == 100.0

print("Raw frame interval analyzer tests passed (exact percentiles/thresholds, hashes, 8 rejected malformed traces)")
