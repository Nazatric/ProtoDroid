#!/usr/bin/env bash
# Build and run the exact-production XAudio2 lifecycle regression in both master-reuse and quantum modes.
# The pinned FAudio checkout supplies the ABI header; no audio device is opened by this fake-backend test.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
FAUDIO_DIR=${1:-/tmp/protodroid-faudio-test}
OUT=${2:-/tmp/protodroid-xaudio2-test}
CXX=${CXX:-g++}
SOURCE=${XAUDIO2_SOURCE:-$ROOT/src/native/xaudio2.cpp}

if [[ ! -f "$FAUDIO_DIR/include/FAudio.h" ]]; then
    echo "FAudio 25.10 header not found under $FAUDIO_DIR" >&2
    exit 2
fi
if [[ ! -f "$SOURCE" ]]; then
    echo "XAudio2 source not found: $SOURCE" >&2
    exit 2
fi

for reuse in 1 0; do
    for quantum in 0 1; do
        suffix="reuse${reuse}-quantum${quantum}"
        binary="${OUT}-${suffix}"
        "$CXX" -std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
            -fsanitize=address,undefined -fno-omit-frame-pointer \
            -DRECOMP_FLAT_MEMORY -DRECOMP_GUEST_BASE=0x200000000000ull \
            -DFAKE_MASTER_REUSE="$reuse" -DFAKE_LONG_QUANTUM="$quantum" \
            -DXAUDIO2_SOURCE="\"$SOURCE\"" \
            -I"$FAUDIO_DIR/include" -I"$ROOT/src/native" -I"$ROOT/src/recomp" \
            "$ROOT/src/native/tests/xaudio2_test.cpp" -pthread -o "$binary"
        # Production intentionally retains guest vtables/blocks for process lifetime; keep address and UB checks on.
        ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 "$binary"
    done
done
