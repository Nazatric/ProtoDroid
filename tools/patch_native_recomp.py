#!/usr/bin/env python3
"""Apply exact, runtime-gated native feature policies after engine recompilation.

The original x86 instruction remains documented in the generated comment. Ordinary Play takes the same branch; only
remove_tick_rate_limit=1 suppresses the 15 ms skip. The patch is intentionally narrow and idempotent.
"""
from pathlib import Path
import sys

OLD = "    if (rf.cf) { goto L_7c897c5d; }  /* 7c897bf8: jb 0x7c897c5d */"
NEW = "    if (rf.cf && !RecompRemoveTickRateLimit()) { goto L_7c897c5d; }  /* 7c897bf8: jb 0x7c897c5d */"


def apply(root: Path) -> None:
    target = root / "part_0089.c"
    if not target.is_file():
        raise SystemExit(f"native feature patch: missing {target}")
    text = target.read_text()
    old_count = text.count(OLD)
    new_count = text.count(NEW)
    if old_count == 1 and new_count == 0:
        target.write_text(text.replace(OLD, NEW))
        print(f"native feature patch: applied simulation gate policy to {target}")
        return
    if old_count == 0 and new_count == 1:
        print(f"native feature patch: already applied to {target}")
        return
    raise SystemExit(
        f"native feature patch: expected exactly one tick gate, found original={old_count} patched={new_count}"
    )


if __name__ == "__main__":
    apply(Path(sys.argv[1] if len(sys.argv) > 1 else "build/recomp/native"))
