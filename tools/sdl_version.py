#!/usr/bin/env python3
"""Verify one SDL2 version across source, Java bytecode, and native build provenance."""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re


def _number(text: str, pattern: str, label: str) -> int:
    match = re.search(pattern, text, re.MULTILINE | re.DOTALL)
    if not match:
        raise ValueError(f"missing {label}")
    return int(match.group(1))


def source_version(root: Path) -> str:
    """Return the SDL version only when the C headers and Android Java constants agree."""
    root = Path(root)
    header = (root / "include/SDL_version.h").read_text(encoding="utf-8")
    activity = (root / "android-project/app/src/main/java/org/libsdl/app/SDLActivity.java").read_text(
        encoding="utf-8"
    )
    c_parts = tuple(
        _number(header, rf"^\s*#define\s+SDL_{macro}\s+(\d+)\s*$", f"SDL_{macro}")
        for macro in ("MAJOR_VERSION", "MINOR_VERSION", "PATCHLEVEL")
    )
    java_parts = tuple(
        _number(
            activity,
            rf"SDL_{name}_VERSION\s*=\s*(\d+)\s*;",
            f"Java SDL_{name}_VERSION",
        )
        for name in ("MAJOR", "MINOR", "MICRO")
    )
    if c_parts != java_parts:
        c_version = ".".join(map(str, c_parts))
        java_version = ".".join(map(str, java_parts))
        raise ValueError(f"SDL source mismatch: C headers {c_version}, Android Java {java_version}")
    return ".".join(map(str, c_parts))


def pinned_version(build_script: Path) -> str:
    text = Path(build_script).read_text(encoding="utf-8")
    match = re.search(r"^SDL2_TAG=release-(\d+\.\d+\.\d+)\s*$", text, re.MULTILINE)
    if not match:
        raise ValueError(f"no exact SDL2_TAG pin in {build_script}")
    return match.group(1)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _require_version(actual: str, expected: str) -> None:
    if actual != expected:
        raise ValueError(f"SDL version mismatch: expected {expected}, got {actual}")


def write_manifest(source_root: Path, library: Path, output: Path, expected: str) -> None:
    actual = source_version(Path(source_root))
    _require_version(actual, expected)
    library = Path(library)
    if not library.is_file() or library.stat().st_size == 0:
        raise ValueError(f"missing native SDL library {library}")
    Path(output).write_text(f"version={actual}\nsha256={_sha256(library)}\n", encoding="utf-8")


def verify_manifest(manifest: Path, library: Path, expected: str) -> None:
    values = {}
    for line in Path(manifest).read_text(encoding="utf-8").splitlines():
        key, separator, value = line.partition("=")
        if not separator or key in values:
            raise ValueError(f"invalid SDL provenance line in {manifest}: {line!r}")
        values[key] = value
    if set(values) != {"version", "sha256"}:
        raise ValueError(f"invalid SDL provenance keys in {manifest}: {sorted(values)}")
    _require_version(values["version"], expected)
    actual_hash = _sha256(library)
    if values["sha256"] != actual_hash:
        raise ValueError(
            f"native SDL provenance mismatch for {library}: manifest {values['sha256']}, actual {actual_hash}"
        )


def verify_class(javap_output: Path, expected: str) -> None:
    text = Path(javap_output).read_text(encoding="utf-8")
    parts = tuple(
        _number(
            text,
            rf"SDL_{name}_VERSION;.*?ConstantValue:\s+int\s+(\d+)",
            f"compiled Java SDL_{name}_VERSION ConstantValue",
        )
        for name in ("MAJOR", "MINOR", "MICRO")
    )
    _require_version(".".join(map(str, parts)), expected)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    pin = sub.add_parser("pin")
    pin.add_argument("build_script", type=Path)

    source = sub.add_parser("source")
    source.add_argument("root", type=Path)
    source.add_argument("expected")

    write = sub.add_parser("write-manifest")
    write.add_argument("source_root", type=Path)
    write.add_argument("library", type=Path)
    write.add_argument("output", type=Path)
    write.add_argument("expected")

    manifest = sub.add_parser("verify-manifest")
    manifest.add_argument("manifest", type=Path)
    manifest.add_argument("library", type=Path)
    manifest.add_argument("expected")

    compiled = sub.add_parser("verify-class")
    compiled.add_argument("javap_output", type=Path)
    compiled.add_argument("expected")

    args = parser.parse_args()
    try:
        if args.command == "pin":
            print(pinned_version(args.build_script))
        elif args.command == "source":
            actual = source_version(args.root)
            _require_version(actual, args.expected)
            print(f"Verified SDL C/Java source version {actual}")
        elif args.command == "write-manifest":
            write_manifest(args.source_root, args.library, args.output, args.expected)
        elif args.command == "verify-manifest":
            verify_manifest(args.manifest, args.library, args.expected)
        elif args.command == "verify-class":
            verify_class(args.javap_output, args.expected)
    except (OSError, ValueError) as error:
        raise SystemExit(f"ERROR: {error}") from error


if __name__ == "__main__":
    main()
