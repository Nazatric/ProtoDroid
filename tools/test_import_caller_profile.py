#!/usr/bin/env python3
"""Structural gates for opt-in bounded D3D9 guest-caller attribution."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
IMPORTS = (ROOT / "src/native/imports.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "src/native/import_caller_profile.h").read_text(encoding="utf-8")
MAKEFILE = (ROOT / "src/native/Makefile").read_text(encoding="utf-8")
ANALYZER = (ROOT / "tools/analyze_import_callers.py").read_text(encoding="utf-8")
RUNTIME = (ROOT / "tools/test_import_caller_profile_runtime.py").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/build-apk.yml").read_text(encoding="utf-8")
INI = (ROOT / "android/protodroid.ini.example").read_text(encoding="utf-8")


def validate(imports: str, header: str, makefile: str, analyzer: str, runtime: str, workflow: str, ini: str) -> int:
    count = 0

    def check(condition: bool, message: str) -> None:
        nonlocal count
        count += 1
        if not condition:
            raise AssertionError(message)

    check("kMaxEntries = 2048" in header, "bounded caller entries")
    check("(uint64_t(import_index) << 32) | caller_rva" in header, "compound import/caller key")
    check("entries_.size() >= kMaxEntries" in header and "overflow_[import_index].total_ns" in header and
          "++overflow_[import_index].calls" in header, "overflow retains time and calls")
    check("void Reset()" in header and "entries_.clear()" in header and "lookup_.clear()" in header and
          "overflow_.clear()" in header, "complete per-window reset")
    builder = header.split("BuildImportCallerLines", 1)[1]
    check("i < callers.size() && i < 3" in builder, "top-three output bound")
    check("a->caller_rva < b->caller_rva" in builder, "deterministic caller tie break")
    check("method.total_ns - listed_ns" in builder and "method.total_calls - listed_calls" in builder,
          "exact unlisted residuals")
    check("caller=\" + std::string(rva)" in builder and "std::snprintf" in builder,
          "fixed-width caller RVA formatting")

    profile = imports.split("void ProfiledCall", 1)[1].split("\n}\n\n}  // namespace", 1)[0]
    check("const bool track_callers = s.dll == \"d3d9.dll\"" in profile, "D3D9-only caller tracking")
    check("const uint32_t return_address = SR32(ctx->esp)" in profile and
          "caller_rva = return_address - g_engine_lo" in profile, "pre-pop guest return RVA")
    check(profile.index("SR32(ctx->esp)") < profile.index("s.fn(ctx)"), "return address captured before import")
    check(profile.index("tp.callers.Add(index, caller_rva, duration)") > profile.index("s.fn(ctx)"),
          "caller duration recorded after import")
    check("LogCallerStats(tp, order)" in profile and "tp.callers.Reset()" in profile and
          profile.index("LogCallerStats(tp, order)") < profile.index("tp.callers.Reset()"),
          "caller output precedes reset")
    log_stats = imports.split("void LogCallerStats", 1)[1].split("void ProfiledCall", 1)[0]
    check("k < order.size() && k < 10" in log_stats and "slots[index].dll == \"d3d9.dll\"" in log_stats,
          "existing top-ten method bound and D3D9 filter")
    check("BuildImportCallerLines(tp.callers" in log_stats, "production uses tested formatter")
    dispatch = imports.split("bool DispatchImport", 1)[1]
    check("if (ImportProfiling())" in dispatch and "if (s.name != \"SwitchToFiber\")" in dispatch and
          "ProfiledCall(s, index, ctx);" in dispatch and "\n    fn(ctx);" in dispatch,
          "ordinary dense dispatch remains unprofiled and fiber switching remains excluded")
    check("ImportCallerProfile callers" in imports and "thread_local ThreadProfile tp" in profile,
          "caller storage exists only in profiling thread-local state")

    check("$(OUT)/imports.o: import_caller_profile.h" in makefile, "incremental header dependency")
    check("caller_rva - 1" in analyzer and "containing-function" in analyzer,
          "offline return-address extent symbolization")
    check("self.rows[index - 1][\"start\"] == start" in analyzer and
          '"ambiguous" if candidates else "unresolved"' in analyzer, "ambiguous symbols are not guessed")
    check("listed plus unlisted accounting mismatch" in analyzer and "len(callers) > 3" in analyzer,
          "analyzer validates residual accounting and output bound")
    check("re/functions.csv" in runtime and "--emit-golden" in runtime and
          "real formatter, analyzer, real functions.csv" in runtime, "real end-to-end golden gate")
    check("test_import_caller_profile.py" in workflow and "test_import_caller_profile_runtime.py" in workflow and
          "test_analyze_import_callers.py" in workflow, "CI coverage")
    check("#import_profile = 1" in ini and "bounded top-three immediate caller RVAs" in ini,
          "opt-in documentation and limitation")
    return count


def expect_detection(description: str, **changes: str) -> None:
    values = dict(imports=IMPORTS, header=HEADER, makefile=MAKEFILE, analyzer=ANALYZER,
                  runtime=RUNTIME, workflow=WORKFLOW, ini=INI)
    values.update(changes)
    try:
        validate(**values)
    except AssertionError:
        return
    raise AssertionError(f"negative control not detected: {description}")


total = validate(IMPORTS, HEADER, MAKEFILE, ANALYZER, RUNTIME, WORKFLOW, INI)
expect_detection("cap removed", header=HEADER.replace("kMaxEntries = 2048", "kMaxEntries = 0", 1))
expect_detection("post-pop capture", imports=IMPORTS.replace("const uint32_t return_address = SR32(ctx->esp)",
                                                             "const uint32_t return_address = 0", 1))
expect_detection("ordinary path profiled", imports=IMPORTS.replace("\n    fn(ctx);",
                                                                   "\n    ProfiledCall(Slots()[index], index, ctx);", 1))
expect_detection("residual removed", header=HEADER.replace("method.total_ns - listed_ns", "0", 1))
expect_detection("dependency removed", makefile=MAKEFILE.replace("$(OUT)/imports.o: import_caller_profile.h", "", 1))
expect_detection("CI removed", workflow=WORKFLOW.replace("python3 tools/test_analyze_import_callers.py", "", 1))
print(f"Import caller-profile structural checks passed ({total}; 6 negative controls detected)")
