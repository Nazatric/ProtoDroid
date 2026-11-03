#!/usr/bin/env bash
set -euo pipefail

BUILD=${1:-/tmp/protodroid-script-ci}
rm -rf "$BUILD"
mkdir -p "$BUILD/lua"
if [[ ${PROTODROID_SCRIPT_ASAN:-0} == 1 ]]; then
  SANITIZERS="-fsanitize=address,undefined"
  GUEST_BASE="0x200000000000ull"
  export ASAN_OPTIONS=detect_leaks=0
else
  SANITIZERS="-fsanitize=undefined"
  GUEST_BASE="0x1000000000ull"
fi
CXXFLAGS_TEST="-std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter $SANITIZERS -fno-sanitize-recover=all -DRECOMP_FLAT_MEMORY -DRECOMP_GUEST_BASE=$GUEST_BASE -DPROTODROID_DOBBY -Isrc/native/tests/fake_dobby -Isrc/native -Isrc/recomp"
LUA_SOURCES=(
  lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes
  lparser lstate lstring ltable ltm lundump lvm lzio
  lauxlib lbaselib lmathlib lstrlib ltablib lutf8lib
)
for unit in "${LUA_SOURCES[@]}"; do
  gcc -std=c99 -O1 -g $SANITIZERS -fno-sanitize-recover=all \
    -Ithird_party/lua-5.4.8 -c "third_party/lua-5.4.8/$unit.c" -o "$BUILD/lua/$unit.o"
done
g++ $CXXFLAGS_TEST \
  src/native/mod_hook.cpp src/native/mod_settings.cpp src/native/mod_symbols.cpp src/native/mod_scripts.cpp \
  src/native/tests/mod_script_test.cpp "$BUILD"/lua/*.o -lm -o "$BUILD/mod_script_test"
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
"$BUILD/mod_script_test"
