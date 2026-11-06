#!/usr/bin/env python3
"""Verify generation-time guest trace coverage and balanced exits in split recompiler output."""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys

IMAGE_BASE = 0x10000000
DEFINITION = re.compile(r"^void f_([0-9a-f]{8})\(Ctx\* c\) \{$")
ENTER = re.compile(r"RecompGuestFunctionEnter\(c, 0x([0-9a-f]{8})u\);")
EXIT = re.compile(r"RecompGuestFunctionExit\(c, 0x([0-9a-f]{8})u\); return;")


def selected_rvas(path: Path) -> set[int]:
    selected = set()
    for line_number, line in enumerate(path.read_text().splitlines(), 1):
        token = line.split("#", 1)[0].strip()
        if not token:
            continue
        try:
            value = int(token, 16)
        except ValueError as error:
            raise ValueError(f"{path}:{line_number}: invalid function address") from error
        rva = value - IMAGE_BASE if value >= IMAGE_BASE else value
        if not 0 < rva < IMAGE_BASE:
            raise ValueError(f"{path}:{line_number}: invalid original-image RVA")
        if rva in selected:
            raise ValueError(f"{path}:{line_number}: duplicate function RVA {rva:08x}")
        selected.add(rva)
    if not selected:
        raise ValueError(f"{path}: empty trace set")
    return selected


def definitions(path: Path) -> dict[str, str]:
    found: dict[str, str] = {}
    current: list[str] | None = None
    name = ""
    for line in path.read_text().splitlines():
        match = DEFINITION.match(line)
        if match:
            if current is not None:
                raise ValueError(f"{path}: nested generated function definition")
            current = [line]
            name = match.group(1)
            continue
        if current is not None:
            current.append(line)
            if line == "}":
                if name in found:
                    raise ValueError(f"{path}: duplicate generated function {name}")
                found[name] = "\n".join(current)
                current = None
    if current is not None:
        raise ValueError(f"{path}: unterminated generated function {name}")
    return found


def verify(root: Path, selection: Path) -> dict[str, int]:
    expected = selected_rvas(selection)
    files = sorted(root.glob("*.c")) if root.is_dir() else [root]
    if not files:
        raise ValueError(f"{root}: no generated C files")
    traced: dict[int, int] = {}
    for path in files:
        for name, body in definitions(path).items():
            enters = ENTER.findall(body)
            exits = EXIT.findall(body)
            if not enters and not exits:
                continue
            if len(enters) != 1:
                raise ValueError(f"{path}: f_{name} has {len(enters)} trace entries")
            rva = int(enters[0], 16)
            if rva in traced:
                raise ValueError(f"{path}: duplicate trace RVA {rva:08x}")
            if rva not in expected:
                raise ValueError(f"{path}: unselected trace RVA {rva:08x}")
            if exits != [f"{rva:08x}"] * len(exits):
                raise ValueError(f"{path}: f_{name} has an exit with the wrong RVA")
            returns = len(re.findall(r"\breturn;", body))
            if returns == 0 or len(exits) != returns:
                raise ValueError(
                    f"{path}: f_{name} has {returns} returns but {len(exits)} traced exits")
            traced[rva] = len(exits)
    missing = sorted(expected - set(traced))
    if missing:
        raise ValueError("missing traced functions: " + ", ".join(f"{rva:08x}" for rva in missing))
    return {"functions": len(traced), "exits": sum(traced.values())}


def main() -> int:
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("generated", type=Path, help="generated .c file or split output directory")
    parser.add_argument("--selection", type=Path, default=repo / "re/loading_boundary_trace.txt")
    args = parser.parse_args()
    try:
        result = verify(args.generated, args.selection)
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(f"guest trace verified: {result['functions']} functions, {result['exits']} balanced returns")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
