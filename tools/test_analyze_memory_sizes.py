#!/usr/bin/env python3
from pathlib import Path
import hashlib
import json
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import analyze_memory_sizes as analyzer

checks = 0


def check(condition, message):
    global checks
    checks += 1
    if not condition:
        raise AssertionError(message)


def record(sizes, name="memcpy", tid=42):
    counts = [0] * len(analyzer.LABELS)
    for size in sizes:
        for index, (low, high) in enumerate(analyzer.BOUNDS):
            if size >= low and (high is None or size <= high):
                counts[index] += 1
                break
        else:
            raise AssertionError(size)
    bins = ",".join(f"{label}:{count}" for label, count in zip(analyzer.LABELS, counts))
    calls = len(sizes)
    return (f"[mono=100.000 tid={tid}] memory-sizes tid {tid} name={name} calls={calls} import_calls={calls} "
            f"total_bytes={sum(sizes)} min={min(sizes)} max={max(sizes)} bins={bins}")


def reject(line, description):
    global checks
    checks += 1
    try:
        analyzer.parse_record(line, "fixture", 1)
    except ValueError:
        return
    raise AssertionError(f"accepted malformed {description}")


valid = record([0, 0, 0, 1, 1, 1, 1, 1, 16, 16, 1000000])
check(analyzer.parse_record(valid, "fixture", 1)["calls"] == 11, "valid call count")
check(analyzer.parse_record(valid, "fixture", 1)["bins"]["1-8"] == 5, "valid bin parse")
check(analyzer.parse_record(valid, "fixture", 1)["mono_s"] == 100.0, "monotonic timestamp")
reject(valid.replace(" calls=11 ", " calls=12 ", 1), "call mismatch")
reject(valid.replace(" import_calls=11 ", " import_calls=12 ", 1), "import mismatch")
reject(valid.rsplit(",", 1)[0], "missing bin")
pieces = valid.split(" bins=", 1)
bins = pieces[1].split(",")
reject(pieces[0] + " bins=" + ",".join([bins[1], bins[0], *bins[2:]]), "misordered bins")
reject(valid.replace(" total_bytes=1000037 ", " total_bytes=11000001 ", 1), "impossible byte total")
reject(valid.replace(" min=0 ", " min=2 ", 1), "minimum outside first populated bin")
reject(valid.replace(" max=1000000 ", " max=8 ", 1), "maximum outside last populated bin")
reject(valid.replace(" name=memcpy ", " name=memchr ", 1), "unknown operation")
zero_bins = ",".join(f"{label}:0" for label in analyzer.LABELS)
reject(valid.split(" bins=", 1)[0] + " bins=" + zero_bins, "zeroed bins")
reject(valid.replace(" total_bytes=1000037 ", " total_bytes=unknown ", 1), "non-numeric bytes")
zero = record([0, 0], name="memset", tid=43)
check(analyzer.parse_record(zero, "fixture", 1)["total_bytes"] == 0, "legitimate all-zero sizes")

with tempfile.TemporaryDirectory(prefix="protodroid-memory-size-analysis-") as directory:
    path = Path(directory) / "native.log"
    path.write_text("header\n" + valid + "\n" + zero + "\n", encoding="utf-8")
    report = analyzer.analyze([path])
    raw = path.read_bytes()
    check(report["record_count"] == 2, "file record count")
    check(report["files"][0]["sha256"] == hashlib.sha256(raw).hexdigest(), "input hash")
    check(report["operations"]["memcpy"]["calls"] == 11, "aggregate memcpy calls")
    check(report["operations"]["memset"]["calls"] == 2, "aggregate memset calls")
    check(report["operations"]["memset"]["average_bytes_per_call"] == 0, "zero-size average")
    check(sum(report["operations"]["memcpy"]["bins"].values()) == 11, "aggregate bin conservation")
    json.dumps(report)

print(f"memory-size analyzer tests passed ({checks} checks; malformed records rejected)")
