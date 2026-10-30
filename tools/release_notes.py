#!/usr/bin/env python3
"""Print one version's notes from CHANGELOG.md for a GitHub release."""

from __future__ import annotations

import argparse
from pathlib import Path
import re


def notes(text: str, version: str) -> str:
    if not re.fullmatch(r"[0-9]+\.[0-9]+", version):
        raise ValueError("version must use major.minor form")
    heading = re.compile(r"^## ([0-9]+\.[0-9]+)(?:\s+-\s+([0-9]{4}-[0-9]{2}-[0-9]{2}))?\s*$",
                         re.MULTILINE)
    matches = list(heading.finditer(text))
    selected = [match for match in matches if match.group(1) == version]
    if len(selected) != 1:
        raise ValueError(f"expected exactly one changelog section for {version}")
    match = selected[0]
    start = match.end()
    next_heading = heading.search(text, start)
    end = next_heading.start() if next_heading else len(text)
    body = text[start:end].strip()
    if not body:
        raise ValueError(f"changelog section for {version} is empty")
    return body + "\n"


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version")
    parser.add_argument("changelog", nargs="?", type=Path, default=root / "CHANGELOG.md")
    args = parser.parse_args()
    try:
        body = notes(args.changelog.read_text(encoding="utf-8"), args.version)
    except (OSError, ValueError) as error:
        parser.exit(1, f"release notes: {error}\n")
    print(body, end="")


if __name__ == "__main__":
    main()
