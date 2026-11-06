#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("analyze_guest_function_trace", ROOT / "tools/analyze_guest_function_trace.py")
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def record(sequence: int, rva: int, start: int, duration: int, *, event: str = "complete",
           arg0: int = 0, arg1: int = 0, arg2: int = 0, regression: int = 0,
           discarded: int = 0) -> str:
    return (
        f"[mono=1.000 tid=7] guest-function-v1 event={event} sequence={sequence} rva={rva:08x} "
        f"start_ns={start} end_ns={start + duration} duration_ns={duration} depth=0 "
        f"discarded_inner={discarded} clock_regression={regression} ecx=00000000 caller=01020304 "
        f"arg0={arg0:08x} arg1={arg1:08x} arg2={arg2:08x}"
    )


def functions_csv(path: Path) -> None:
    path.write_text(
        "entry,size,is_thunk,namespace,name,source_files\n"
        "104e5250,2426,false,proto::InGameState,vfunction1_for_GameState,\n"
        "10838b40,53,false,Global,FUN_10838b40,\n"
    )


def expect_failure(path: Path, functions: Path, text: str, expected: str) -> None:
    path.write_text(text)
    try:
        MODULE.analyze([path], functions)
        raise AssertionError(f"malformed input accepted: {expected}")
    except ValueError as error:
        require(expected in str(error), f"wrong failure for {expected}: {error}")


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="protodroid-guest-trace-analysis-") as temporary:
        root = Path(temporary)
        functions = root / "functions.csv"
        functions_csv(functions)
        log = root / "valid.log"
        lines = [
            record(1, 0x4E5250, 1000, 100),
            record(2, 0x4E5250, 2000, 200),
            record(3, 0x4E5250, 3000, 300),
            record(4, 0x838B40, 4000, 20, arg0=2),
            record(5, 0x838B40, 5000, 30, arg0=2),
            record(6, 0x838B40, 6000, 40, arg0=3, arg1=2),
        ]
        log.write_text("unrelated log line\n" + "\n".join(lines) + "\n")
        report = MODULE.analyze([log], functions)
        require(report["status"] == "valid-guest-function-records", "valid capture was anomalous")
        require(report["record_count"] == 6 and report["complete_count"] == 6, "record counts differ")
        first = report["functions"][0]
        require(first["rva"] == "004e5250" and first["calls"] == 3 and first["total_ns"] == 600,
                "exact function totals differ")
        require(first["min_ns"] == 100 and first["p50_ns"] == 200 and first["p95_ns"] == 300 and
                first["p99_ns"] == 300 and first["max_ns"] == 300, "nearest-rank statistics differ")
        require(first["symbol"] == "proto::InGameState::vfunction1_for_GameState",
                "unique function symbolization failed")
        requests = report["game_state_request_wrapper_arguments"]
        require(len(requests) == 2 and requests[0]["arg0"] == "00000002" and requests[0]["calls"] == 2 and
                requests[1]["arg0"] == "00000003" and requests[1]["arg1"] == "00000002" and
                requests[1]["calls"] == 1, "raw request argument aggregation differs")
        require(report["input_files"][0]["sha256"] == MODULE.sha256(log.read_bytes()), "input hash differs")

        anomaly = root / "anomaly.log"
        anomaly.write_text(lines[0] + "\nguest-function-v1 event=cap suppressed_records_at_least=1\n")
        report = MODULE.analyze([anomaly], functions)
        require(report["status"] == "anomalous-guest-function-records" and
                report["anomaly_counts"] == {"cap": 1}, "cap did not mark capture anomalous")

        bad = root / "bad.log"
        expect_failure(bad, functions, lines[0].replace("duration_ns=100", "duration_ns=101"),
                       "duration/timestamp mismatch")
        expect_failure(bad, functions, lines[0] + " rva=004e5250\n", "duplicate/empty field")
        expect_failure(bad, functions, lines[0] + " unknown=1\n", "wrong complete fields")
        expect_failure(bad, functions, lines[0].replace("sequence=1", "sequence=0"), "out-of-range sequence")
        expect_failure(bad, functions, lines[0] + "\n" + lines[0], "duplicate sequence 1")
        expect_failure(bad, functions, lines[0].replace("clock_regression=0", "clock_regression=1"),
                       "inconsistent regressed clock")
        expect_failure(bad, functions, record(1, 0x4E5250, 10, 1, event="mismatch"),
                       "non-complete event has a duration")
        expect_failure(bad, functions, lines[0].replace("rva=004e5250", "rva=zzzzzzzz"), "invalid rva")
        expect_failure(bad, functions, lines[0] + " trailing\n", "malformed token")
        expect_failure(bad, functions, "nothing here\n", "no guest-function-v1 records")
    print("guest function trace analyzer tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
