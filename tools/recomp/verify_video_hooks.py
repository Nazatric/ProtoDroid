#!/usr/bin/env python3
"""Verify the generated GameDB video-setting call-site hooks.

The engine's generated C is checked into the repository and can also be restored from CI's
recompilation cache. Checking the Python emitter alone is therefore insufficient: this script
checks the exact C files that the Android build compiles.
"""
from pathlib import Path
import re
import sys

EXPECTED = {
    "7c2e352f": "Anti-Alias Level",
    "7c2e357c": "Shadow Detail",
    "7c2e35a0": "Graphic Quality",
}
BOOLEAN_SITE = "7c2e3553"
GETTER = "0x7c7f87d0u"


def fail(message: str) -> None:
    raise SystemExit(f"generated video-hook verification failed: {message}")


def main() -> None:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/recomp/native")
    if not root.is_dir():
        fail(f"{root} is not a generated-source directory")

    hooked = {}
    getter_sites = set()
    total_hooks = 0
    for path in sorted(root.glob("*.c")):
        with path.open(encoding="utf-8") as source:
            for line_number, line in enumerate(source, 1):
                total_hooks += line.count("RecompVideoSettingRead(c)")
                if f"call {GETTER[:-1]}" not in line:
                    if "RecompVideoSettingRead(c)" in line:
                        fail(f"hook outside a getter call at {path}:{line_number}")
                    continue
                match = re.search(r"/\* ([0-9a-f]{8}): call 0x7c7f87d0 \*/", line)
                if not match:
                    continue
                site = match.group(1)
                getter_sites.add(site)
                has_hook = "RecompVideoSettingRead(c)" in line
                if site == BOOLEAN_SITE and has_hook:
                    fail(f"Boolean-setting call {site} was hooked at {path}:{line_number}")
                if not has_hook:
                    continue
                if site not in EXPECTED:
                    fail(f"unexpected hook at {site} ({path}:{line_number})")
                if site in hooked:
                    fail(f"duplicate hook at {site}")
                push = line.find("LPush32(&r_esp,")
                hook = line.find("RecompVideoSettingRead(c)")
                direct = line.find("if (RecompDirect(0x7c7f87d0u))")
                dispatch = line.find("RecompDispatch(c, 0x7c7f87d0u)")
                original = direct if direct >= 0 else dispatch
                if push < 0 or original < 0 or not (push < hook < original):
                    fail(f"hook is not between Push32 and the original getter at {site}")
                hooked[site] = (path.name, line_number)

    if total_hooks != 3:
        fail(f"expected exactly three generated hook calls, found {total_hooks}")
    missing = set(EXPECTED) - set(hooked)
    if missing:
        fail(f"missing expected hooks: {', '.join(sorted(missing))}")
    if len(hooked) != 3:
        fail(f"expected exactly three hooks, found {len(hooked)}")
    if BOOLEAN_SITE not in getter_sites:
        fail(f"Boolean getter call {BOOLEAN_SITE} was not found; xref verification is stale")

    print("generated video hooks verified: exactly AA/shadow/quality; Boolean call untouched; generic getter unhooked")
    for site, (name, line) in sorted(hooked.items()):
        print(f"  {site} {EXPECTED[site]}: {name}:{line}")


if __name__ == "__main__":
    main()
