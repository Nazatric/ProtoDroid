#!/usr/bin/env python3
"""Turn a build log into GitHub check annotations.

The workflow log is not always retrievable (its blob host can be unreachable from a reviewer's environment), and a step
that fails with "Process completed with exit code 1" and nothing else is not a diagnosis. This prints the interesting
lines of a log file as `::error::` annotations, with a few lines of context, so the failure travels with the check run.

Usage: python3 tools/annotate_log.py <log-file> <prefix> [--max N] [--tail N]
Exit status is always 0: this is a reporting step, and it must not mask the real failure.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# Things that mean "this is the failure". Matching is deliberately conservative: a pattern that matches successful
# configure output buries the real cause (every CMake capability probe prints "not found").
FAILURE = re.compile(
    r"error:|error\b.*:|CMake Error|Traceback|FAILED|fatal error|undefined reference|NOT FOUND|No such file|"
    r"cannot find|command not found|ninja: build stopped|Execution failed|What went wrong",
    re.IGNORECASE,
)
# Upper-case NOT FOUND is a CMake hard failure; lower-case is a successful probe, so it is excluded above by the extra
# pass below.
PROBE = re.compile(r"^-- Looking for .* - not found$")


def interesting(lines: list[str]) -> list[str]:
    chosen: list[str] = []
    for index, line in enumerate(lines):
        if PROBE.match(line):
            continue
        if FAILURE.search(line):
            chosen.extend(lines[max(0, index - 2):index + 3])
    return chosen


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("log")
    parser.add_argument("prefix")
    parser.add_argument("--max", type=int, default=30)
    parser.add_argument("--tail", type=int, default=40)
    args = parser.parse_args()

    path = Path(args.log)
    if not path.is_file():
        print(f"::error::{args.prefix}: no log at {args.log}")
        return 0
    lines = path.read_text(errors="replace").splitlines()
    chosen = interesting(lines)
    if not chosen:
        chosen = lines[-args.tail:]
        print(f"::warning::{args.prefix}: no failure pattern matched; showing the last {len(chosen)} lines")
    for line in chosen[-args.max:]:
        print(f"::error::{args.prefix}: " + line[:1800].replace("%", "%25").replace("\r", "%0D"))
    print(f"::error::{args.prefix}: {len(chosen)} interesting line(s) in {len(lines)} lines of {args.log}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
