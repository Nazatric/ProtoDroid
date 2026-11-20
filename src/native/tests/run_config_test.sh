#!/usr/bin/env bash
# protodroid.ini precedence checks (config.cpp): host build, no Android dependencies.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
OUT=${1:-/tmp/protodroid-config-test}
CXX=${CXX:-g++}

"$CXX" -std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -DRECOMP_FLAT_MEMORY -DRECOMP_GUEST_BASE=0x1000000000ull -I"$ROOT/src/native" \
  "$ROOT/src/native/config.cpp" "$ROOT/src/native/tests/config_test.cpp" -o "$OUT"
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 "$OUT"
