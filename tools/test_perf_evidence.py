#!/usr/bin/env python3
"""Fixtures for performance-evidence extraction and controlled-comparison guards."""
from pathlib import Path
import importlib.util
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("perf_evidence", ROOT / "tools/perf_evidence.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

A = "a" * 40
B = "b" * 40
BASE = f"""[mono=1.000 tid=7] protodroid source {A}
[mono=1.001 tid=7] device: model=Phone device=phone soc=SOC platform=plat hardware=plat android=16 sdk=36 abi=arm64-v8a
[mono=1.002 tid=7] vulkan: system driver sees Test GPU (profile test, device Vulkan 1.3.1)
[mono=1.003 tid=7] config: game_aa_level = -1
[mono=1.100 tid=8] diag: Present wall (includes FIFO/vsync, not GPU time) n=10 avg=20.0 p50=19.0 p95=30.0 p99=40.0 max=40.0 ms; gt8.5/17/34/50/100/250/1000ms=1/1/0/0/0/0/0
[mono=1.200 present=10] phase=render index=0 status=tested phase_present_average_ms=20.000 phase_present_max_ms=40.000 present_window_fps=50.000 present_window_average_ms=20.000 present_window_p99_ms=40.000 present_window_max_ms=40.000 process_cpu_utilization_pct=200.00 wall_ms=1000.000
"""
CANDIDATE = BASE.replace(A, B).replace("20.000", "10.000").replace("50.000", "60.000")

with tempfile.TemporaryDirectory(prefix="protodroid-perf-evidence-") as directory:
    root = Path(directory)
    before_path, after_path = root / "before.log", root / "after.log"
    before_path.write_text(BASE)
    after_path.write_text(CANDIDATE)
    before = module.parse_logs([before_path])
    after = module.parse_logs([after_path])
    assert before["source"] == A and after["source"] == B
    assert before["device"]["model"] == "Phone" and before["gpu"] == "Test GPU"
    assert before["config"] == {"game_aa_level": "-1"}
    assert before["present_windows"][0]["p95"] == 30.0
    comparison = module.compare_summaries(before, after)
    assert comparison["comparable"] and not comparison["warnings"]
    metrics = {entry["field"]: entry for entry in comparison["phases"][0]["metrics"]}
    assert metrics["phase_present_average_ms"]["delta_pct"] == -50.0
    assert metrics["present_window_fps"]["delta_pct"] == 20.0
    changed = module.parse_logs([after_path])
    changed["config"]["game_aa_level"] = "0"
    assert not module.compare_summaries(before, changed)["comparable"]
    after_path.write_text(CANDIDATE.replace("model=Phone", "model=Other"))
    other_device = module.parse_logs([after_path])
    assert not module.compare_summaries(before, other_device)["comparable"]
    after_path.write_text(CANDIDATE.replace("phase=render", "phase=combat"))
    changed_scene = module.parse_logs([after_path])
    assert not module.compare_summaries(before, changed_scene)["comparable"]
    after_path.write_text(CANDIDATE + f"protodroid source {A}\n")
    try:
        module.parse_logs([after_path])
    except ValueError as error:
        assert "multiple source revisions" in str(error)
    else:
        raise AssertionError("mixed source revisions were accepted")

    no_metrics = {**before, "phases": [{"name": "render", "index": 0, "metrics": {}}]}
    no_metrics_after = {**after, "phases": [{"name": "render", "index": 0, "metrics": {}}]}
    assert not module.compare_summaries(no_metrics, no_metrics_after)["comparable"]

real = module.parse_logs([ROOT / "proto-native.log", ROOT / "journal.log"])
assert real["source"] and len(real["source"]) == 40
assert real["device"].get("abi") == "arm64-v8a"
assert real["gpu"] and len(real["present_windows"]) > 10 and len(real["phases"]) >= 20
print("Performance-evidence parsing, real-log coverage, and A/B comparability guards passed")
