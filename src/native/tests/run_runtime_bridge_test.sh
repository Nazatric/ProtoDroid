#!/usr/bin/env bash
# Build and run the production dispatcher/import bridge with a synthetic guest address space.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
OUT=${1:-/tmp/protodroid-runtime-bridge-test}
CXX=${CXX:-g++}

"$CXX" -std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
  -fsanitize=undefined -fno-sanitize-recover=all -DRECOMP_TSO -DRECOMP_GUEST_BASE=0ull \
  -I"$ROOT/src/native" \
  "$ROOT/src/native/runtime.cpp" "$ROOT/src/native/imports.cpp" "$ROOT/src/native/misc.cpp" \
  "$ROOT/src/native/tests/runtime_bridge_test.cpp" -pthread -o "$OUT"
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 "$OUT"

# The import range is disjoint from the engine image. Keep its direct check ahead of the known-miss guest hash probe.
python3 - "$ROOT/src/native/runtime.cpp" <<'PY'
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
dispatch = source.split('extern "C" void RecompDispatch(Ctx* ctx, uint32_t target)', 1)[1]
dispatch = dispatch.split('extern "C" void RecompCpuid', 1)[0]
assert dispatch.index("if (DispatchImport(ctx, target)) return;") < dispatch.index("RecompLookup(target)"), (
    "direct imports must not attempt a known-miss recompiled-function lookup"
)
print("Runtime bridge dispatch-order check passed")
PY

# On a real ARM64 runner, inspect the exact header used by Android. Import stack arguments must compile to plain
# loads, while the shared-memory control must retain an acquire instruction under the established TSO policy.
if [[ $(uname -m) == aarch64 ]]; then
  cat >"${OUT}-codegen.cpp" <<'CPP'
#include "guest.h"
extern "C" __attribute__((noinline)) double ProbeImportArgs(Ctx* c) {
    return native::Arg(c, 0) + native::ArgF(c, 1) + native::ArgD(c, 2);
}
extern "C" __attribute__((noinline)) uint32_t ProbeSharedRead(uint32_t address) {
    return R32(address);
}
CPP
  "$CXX" -std=c++17 -O2 -DRECOMP_FLAT_MEMORY -DRECOMP_GUEST_BASE=0x1000000000ull \
    -DRECOMP_TSO -march=armv8.2-a+rcpc -I"$ROOT/src/native" -S \
    "${OUT}-codegen.cpp" -o "${OUT}-codegen.s"
  python3 - "${OUT}-codegen.s" <<'PY'
from pathlib import Path
import re
import sys

assembly = Path(sys.argv[1]).read_text(encoding="utf-8")

def body(name):
    match = re.search(rf"(?ms)^{re.escape(name)}:\s*\n(.*?)^\s*\.size\s+{re.escape(name)}\b", assembly)
    assert match, f"missing emitted function {name}"
    return match.group(1)

import_args = body("ProbeImportArgs")
shared_read = body("ProbeSharedRead")
acquire = r"\b(?:ldar|ldarb|ldarh|ldapr|ldaprb|ldaprh)\b"
assert "ldr" in import_args, "import argument probe did not emit a load"
assert not re.search(acquire, import_args), "private import arguments retained an ARM64 acquire load"
assert "dmb" not in import_args, "private import arguments retained an ARM64 memory fence"
assert re.search(acquire, shared_read), "shared guest read lost its ARM64 acquire ordering"
print("ARM64 private-stack/shared-memory code-generation checks passed")
PY
fi
