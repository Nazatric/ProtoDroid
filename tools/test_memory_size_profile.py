#!/usr/bin/env python3
"""Ensure CRT memory-size accounting remains inside the existing opt-in import profiler."""
from pathlib import Path
import tempfile
import shutil

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(signature)


def verify(root=ROOT):
    imports = (root / "src/native/imports.cpp").read_text(encoding="utf-8")
    crt = (root / "src/native/msvcr80.cpp").read_text(encoding="utf-8")
    profile = function(imports, "void ProfiledCall")
    dispatch = function(imports, "bool DispatchImport")
    track = function(imports, "bool TrackMemorySize")
    checks = 0

    def check(condition, message):
        nonlocal checks
        checks += 1
        if not condition:
            raise AssertionError(message)

    check('#include "memory_size_profile.h"' in imports, "profile header missing")
    check("MemorySizeProfile memory_sizes;" in imports, "thread profile has no size accumulator")
    check('slot.dll == "MSVCR80.DLL"' in track, "wrong DLL boundary")
    for name in ("memcpy", "memmove", "memset"):
        check(f'slot.name == "{name}"' in track, f"{name} is not covered")
    check("memchr" not in track and "memcpy_s" not in track and "memmove_s" not in track,
          "a different size-argument contract was included")
    check("track_memory_size ? Arg(ctx, 2) : 0" in profile, "the third size argument is not captured")
    check(profile.index("const uint32_t memory_size") < profile.index("s.fn(ctx);") <
          profile.index("tp.memory_sizes.Add(index, memory_size);"),
          "size must be captured before handler stack pops and recorded after return")
    check(profile.index("++tp.calls[index];") < profile.index("tp.memory_sizes.Add(index, memory_size);"),
          "size calls diverge from import call accounting")
    check(profile.index("LogCallerStats(tp, order);") < profile.index("LogMemorySizeStats(tp, order);") <
          profile.index("tp.memory_sizes.Reset();") < profile.index("tp.window = t1;"),
          "window emission/reset order changed")
    check("memory_sizes" not in dispatch and "memory_size" not in dispatch and "MemorySize" not in dispatch,
          "ordinary dispatch touches memory-size profiling")
    check("if (ImportProfiling())" in dispatch and dispatch.index("if (ImportProfiling())") <
          dispatch.rindex("fn(ctx);"), "ordinary/profiled dispatch boundary changed")
    check("C(memcpy) { Return(c, Addr(memmove(Ptr<void>(Arg(c, 0)), Ptr<void>(Arg(c, 1)), Arg(c, 2)))); }" in crt,
          "ordinary memcpy handler changed")
    check("memory_size" not in crt and "MemorySize" not in crt, "CRT hot handlers gained profiling work")
    check("BuildMemorySizeLine" in imports and "import_calls" in
          (root / "src/native/memory_size_profile.h").read_text(encoding="utf-8"),
          "auditable size/import totals missing")
    return checks


def mutations():
    detected = 0
    cases = [
        ("src/native/imports.cpp", "tp.memory_sizes.Reset();", "// reset removed"),
        ("src/native/imports.cpp", 'slot.dll == "MSVCR80.DLL"', 'slot.dll == "d3d9.dll"'),
        ("src/native/imports.cpp", "const uint32_t memory_size = track_memory_size ? Arg(ctx, 2) : 0;",
         "const uint32_t memory_size = 0;"),
        ("src/native/imports.cpp", "    fn(ctx);\n    tl_ctx = saved;",
         "    fn(ctx);\n    MemorySizeProfile ordinary_regression;\n    tl_ctx = saved;"),
    ]
    for relative, old, new in cases:
        with tempfile.TemporaryDirectory(prefix="protodroid-memory-size-mutation-") as directory:
            root = Path(directory)
            (root / "src/native").mkdir(parents=True)
            for name in ("imports.cpp", "msvcr80.cpp", "memory_size_profile.h"):
                shutil.copyfile(ROOT / "src/native" / name, root / "src/native" / name)
            path = root / relative
            text = path.read_text(encoding="utf-8")
            if old not in text:
                raise AssertionError(f"mutation input missing: {old}")
            path.write_text(text.replace(old, new, 1), encoding="utf-8")
            try:
                verify(root)
            except (AssertionError, ValueError):
                detected += 1
            else:
                raise AssertionError(f"mutation escaped: {new}")
    return detected


if __name__ == "__main__":
    print(f"Memory-size profiling boundary checks passed ({verify()})")
    print(f"Memory-size profiling mutation checks passed ({mutations()} detected)")
