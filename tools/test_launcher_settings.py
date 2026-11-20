#!/usr/bin/env python3
"""Every launcher option that claims to be an engine option must have a native reader.

A settings row marked `engine=true` is written into protodroid.ini and then has to be read by the native port. A row
with no reader looks exactly like a broken toggle on the device ("developer menu does nothing", "show FPS does not
work"), and the same class of bug hid behind the ini-precedence problem for a long time. This check fails the build
when a launcher row is written but nothing native ever reads it, and it prints the resolved table so the mapping is
visible in CI.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SETTINGS = ROOT / "android" / "java" / "com" / "protodroid" / "GameSettings.java"
NATIVE_DIRS = [ROOT / "src" / "native"]


def split_top_level(text):
    """Splits a Java argument list on commas that are not inside (), [] or a string literal."""
    args, depth, current, quote = [], 0, [], False
    i = 0
    while i < len(text):
        ch = text[i]
        if quote:
            current.append(ch)
            if ch == '\\':
                i += 1
                if i < len(text):
                    current.append(text[i])
            elif ch == '"':
                quote = False
        elif ch == '"':
            quote = True
            current.append(ch)
        elif ch in "([{":
            depth += 1
            current.append(ch)
        elif ch in ")]}":
            depth -= 1
            current.append(ch)
        elif ch == ',' and depth == 0:
            args.append(''.join(current).strip())
            current = []
        else:
            current.append(ch)
        i += 1
    if current:
        args.append(''.join(current).strip())
    return args


def setting_calls(text):
    """Yields the top-level argument list of every `new Setting(...)` call."""
    for match in re.finditer(r"new Setting\(", text):
        start = match.end()
        depth, i, quote = 1, start, False
        while i < len(text) and depth:
            ch = text[i]
            if quote:
                if ch == '\\':
                    i += 1
                elif ch == '"':
                    quote = False
            elif ch == '"':
                quote = True
            elif ch == '(':
                depth += 1
            elif ch == ')':
                depth -= 1
            i += 1
        yield text[start:i - 1]


def main():
    text = SETTINGS.read_text()
    native = "\n".join(p.read_text() for d in NATIVE_DIRS for p in d.rglob("*") if p.suffix in (".cpp", ".h", ".inc"))

    engine_keys, launcher_keys, missing = [], [], []
    for args in setting_calls(text.split("final Setting[] settings")[1]):
        parts = split_top_level(args)
        if len(parts) < 5:
            raise SystemExit(f"unexpected Setting() shape: {args[:120]}")
        label = re.search(r'"([^"]*)"', parts[0]).group(1)
        key = re.search(r'"([^"]*)"', parts[1]).group(1)
        engine = parts[4].strip() == "true"
        if not engine:
            launcher_keys.append((label, key))
            continue
        engine_keys.append((label, key))
        # The key has to appear as a string literal in the native port: either inline (Setting("key", ...) /
        # SettingInt("key", ...)) or as a row of a table-driven reader (video_settings.cpp maps INI keys to GameDB
        # entries that way). A key that appears nowhere native is a launcher toggle with no effect.
        if f'"{key}"' not in native:
            missing.append((label, key))

    print(f"launcher settings: {len(engine_keys)} engine, {len(launcher_keys)} launcher-only")
    for label, key in engine_keys:
        print(f"  engine   {key:<28} {label}")
    for label, key in launcher_keys:
        print(f"  launcher {key:<28} {label}")
    if missing:
        for label, key in missing:
            print(f"::error::launcher option '{label}' ({key}) is written to protodroid.ini but no native code reads it",
                  file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
