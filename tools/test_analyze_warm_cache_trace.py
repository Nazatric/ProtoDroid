#!/usr/bin/env python3
"""Golden, correlation-isolation, and malformed-input tests for warm-cache analysis."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools/analyze_warm_cache_trace.py"


def guest(sequence: int = 1, start: int = 1000000000, end: int = 2000000000) -> str:
    return (
        f"guest-function-v1 event=complete sequence={sequence} rva=00838b40 start_ns={start} end_ns={end} "
        f"duration_ns={end - start} depth=0 discarded_inner=0 clock_regression=0 ecx=00000000 "
        "caller=00000000 arg0=00000002 arg1=00000000 arg2=00000000"
    )


def warm(at: int, known: int, state: int, total: int, completed: int) -> str:
    return (
        f"DXVK warm-cache snapshot: at_ns={at} state_known={known} state_pending={state} "
        f"pipeline_total={total} pipeline_completed={completed} pipeline_pending={max(total - completed, 0)}"
    )


def valid_log() -> str:
    return "\n".join([
        "unrelated native line",
        guest(),
        warm(500000000, 1, 2, 5, 3),
        warm(1500000000, 0, 0, 6, 4),
        warm(2500000000, 1, 0, 6, 6),
        warm(3000000000, 1, 0, 6, 7),
    ]) + "\n"


def invoke(logs: list[Path], *extra: str) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(TOOL), *map(str, logs), *extra],
                          cwd=ROOT, text=True, capture_output=True)


def success(logs: list[Path], *extra: str) -> dict:
    result = invoke(logs, *extra)
    if result.returncode:
        raise AssertionError(result.stderr)
    return json.loads(result.stdout)


def reject(logs: list[Path], description: str) -> None:
    result = invoke(logs)
    if result.returncode == 0 or "ERROR:" not in result.stderr:
        raise AssertionError(f"malformed input was not rejected: {description}: {result.stdout}")


with tempfile.TemporaryDirectory(prefix="protodroid-warm-analysis-") as temporary:
    root = Path(temporary)
    log = root / "valid.log"
    data = valid_log().encode("utf-8")
    log.write_bytes(data)
    report = success([log])
    assert report["status"] == "valid-warm-cache-snapshots"
    assert report["snapshot_count"] == 4
    assert report["state_unknown_count"] == 1
    assert report["counter_order_inversion_count"] == 1
    assert report["queue_idle_counts"] == {"false": 1, "true": 1, "unknown": 2}
    assert report["max_known_state_pending"] == 2 and report["max_pipeline_pending"] == 2
    assert report["guest_record_count"] == 1 and report["guest_complete_count"] == 1
    assert report["correlated_complete_count"] == 1 and report["uncorrelated_complete_count"] == 0
    assert report["clock_regression_excluded_count"] == 0
    assert report["input_files"][0]["sha256"] == hashlib.sha256(data).hexdigest()
    snapshots = report["warm_cache_snapshots"]
    assert snapshots[1]["state_known"] is False and snapshots[1]["state_pending"] is None
    assert snapshots[1]["state_pending_raw"] == 0 and snapshots[1]["queue_idle"] is None
    assert snapshots[3]["counter_order"] == "completed_exceeds_total"
    assert snapshots[3]["pipeline_total"] == 6 and snapshots[3]["pipeline_completed"] == 7
    assert snapshots[3]["pipeline_pending"] == 0 and snapshots[3]["queue_idle"] is None
    correlation = report["queue_at_guest_function_completion"][0]
    assert correlation["guest_sequence"] == 1 and correlation["rva"] == "00838b40"
    assert correlation["before"] == {"snapshot_id": "0:1", "delta_ns": 500000000}
    assert correlation["after"] == {"snapshot_id": "0:2", "delta_ns": 500000000}
    print("warm analyzer exact snapshot/hash/correlation golden path passed")

    guest_only = root / "guest-only.log"
    guest_only.write_text(guest() + "\n", encoding="utf-8")
    warm_only = root / "warm-only.log"
    warm_only.write_text(warm(1500000000, 1, 1, 2, 1) + "\n", encoding="utf-8")
    isolated = success([guest_only, warm_only])
    assert isolated["snapshot_count"] == 1 and isolated["guest_record_count"] == 1
    assert isolated["correlated_complete_count"] == 0 and isolated["uncorrelated_complete_count"] == 1
    assert isolated["queue_at_guest_function_completion"] == []
    print("warm analyzer rejects implicit cross-input temporal correlation")

    paired = success([], "--pair", str(guest_only), str(warm_only))
    assert paired["correlation_groups"] == [{"input_indices": [0, 1]}]
    assert paired["correlated_complete_count"] == 1 and paired["uncorrelated_complete_count"] == 0
    pair_correlation = paired["queue_at_guest_function_completion"][0]
    assert pair_correlation["correlation_group"] == 0 and pair_correlation["input_index"] == 0
    assert pair_correlation["before"] == {"snapshot_id": "1:0", "delta_ns": 500000000}
    assert pair_correlation["after"] is None
    print("warm analyzer explicit same-run pair correlation passed")

    second_guest = root / "second-guest.log"
    second_guest.write_text(guest() + "\n", encoding="utf-8")
    second_warm = root / "second-warm.log"
    second_warm.write_text(warm(1600000000, 1, 3, 8, 5) + "\n", encoding="utf-8")
    multiple = success([], "--pair", str(guest_only), str(warm_only),
                       "--pair", str(second_guest), str(second_warm))
    assert multiple["correlation_groups"] == [{"input_indices": [0, 1]}, {"input_indices": [2, 3]}]
    assert multiple["correlated_complete_count"] == 2 and multiple["uncorrelated_complete_count"] == 0
    first, second = multiple["queue_at_guest_function_completion"]
    assert first["correlation_group"] == 0 and first["before"]["snapshot_id"] == "1:0"
    assert second["correlation_group"] == 1 and second["before"]["snapshot_id"] == "3:0"
    assert first["before"]["delta_ns"] == 500000000 and second["before"]["delta_ns"] == 400000000
    print("warm analyzer repeatable pairs remain isolated at identical timestamps")

    regressed_line = guest().replace(
        "start_ns=1000000000 end_ns=2000000000 duration_ns=1000000000",
        "start_ns=2000000000 end_ns=1000000000 duration_ns=0").replace(
        "clock_regression=0", "clock_regression=1")
    regressed_log = root / "regressed.log"
    regressed_log.write_text(regressed_line + "\n" + warm(1500000000, 1, 0, 1, 1) + "\n",
                             encoding="utf-8")
    regressed = success([regressed_log])
    assert regressed["guest_anomaly_counts"] == {"clock_regression": 1}
    assert regressed["clock_regression_excluded_count"] == 1
    assert regressed["correlated_complete_count"] == 0 and regressed["uncorrelated_complete_count"] == 0
    print("warm analyzer excludes regressed guest timestamps without hiding the anomaly")

    zero_window = success([log], "--correlation-window-ms", "0")
    assert zero_window["correlated_complete_count"] == 0 and zero_window["uncorrelated_complete_count"] == 1
    output = root / "report.json"
    result = invoke([log], "--output", str(output))
    assert result.returncode == 0 and result.stdout == "" and json.loads(output.read_text())["snapshot_count"] == 4
    print("warm analyzer window and output-file handling passed")

    reject([guest_only], "guest records without any warm snapshot")
    malformed = root / "malformed.log"
    malformed.write_text(valid_log().replace("pipeline_pending=2", "pipeline_pending=3", 1), encoding="utf-8")
    reject([malformed], "pending inconsistent with raw counters")
    malformed.write_text(valid_log().replace("state_known=0", "state_known=2", 1), encoding="utf-8")
    reject([malformed], "invalid state_known")
    malformed.write_text(valid_log().replace("pipeline_pending=2\n", "pipeline_pending=2 extra=1\n", 1),
                         encoding="utf-8")
    reject([malformed], "unknown warm field")
    malformed.write_text(valid_log().replace("duration_ns=1000000000", "duration_ns=999", 1), encoding="utf-8")
    reject([malformed], "malformed guest record in an otherwise valid warm log")
    malformed.write_bytes(valid_log().encode("utf-8") + b"\xff\n")
    reject([malformed], "invalid UTF-8")
    print("warm analyzer malformed-input gates passed (6 cases)")

print("All warm-cache analyzer tests passed")
