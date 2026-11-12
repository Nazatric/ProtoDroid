#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
OUT=${1:-/tmp/protodroid-memory-test}
CXX=${CXX:-g++}

"$CXX" -std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -DRECOMP_FLAT_MEMORY -DRECOMP_GUEST_BASE=0x1000000000ull -I"$ROOT/src/native" \
  "$ROOT/src/native/memory.cpp" "$ROOT/src/native/tests/memory_test.cpp" -o "$OUT"
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 "$OUT"
