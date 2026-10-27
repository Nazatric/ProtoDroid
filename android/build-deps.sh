#!/bin/bash
# Builds the third-party native libraries of the Android port for one ABI: SDL2, FAudio and DXVK (d3d9 only, SDL2
# WSI). Same script locally and on GitHub Actions; nothing here depends on the game files.
#
# Usage: android/build-deps.sh <abi: arm64-v8a | x86_64> <source dir> <install prefix>
# Needs ANDROID_NDK_HOME (or ANDROID_NDK_LATEST_HOME), cmake, ninja, meson, glslang, git.
set -euo pipefail

ABI=${1:?abi}
SRC=$(realpath -m "${2:?source dir}")
PREFIX=$(realpath -m "${3:?install prefix}")
NDK=${ANDROID_NDK_HOME:-${ANDROID_NDK_LATEST_HOME:?set ANDROID_NDK_HOME}}
API=26
JOBS=${JOBS:-$(nproc)}

SDL2_TAG=release-2.30.9
FAUDIO_TAG=25.10
DXVK_TAG=v2.5.3

case $ABI in
    arm64-v8a) TRIPLE=aarch64-linux-android; CPU_FAMILY=aarch64; CPU=armv8-a ;;
    x86_64) TRIPLE=x86_64-linux-android; CPU_FAMILY=x86_64; CPU=x86_64 ;;
    *) echo "unknown ABI $ABI" >&2; exit 2 ;;
esac

fetch() {  # fetch <dir> <url> <tag>
    [ -d "$SRC/$1" ] || git clone -q --depth 1 --branch "$3" --recurse-submodules --shallow-submodules "$2" "$SRC/$1"
}
mkdir -p "$SRC" "$PREFIX"
fetch SDL2 https://github.com/libsdl-org/SDL.git $SDL2_TAG
fetch FAudio https://github.com/FNA-XNA/FAudio.git $FAUDIO_TAG
fetch dxvk https://github.com/doitsujin/dxvk.git $DXVK_TAG

# Android patches (android/patches/<project>-*.patch), applied once.
# git -C changes the working directory, so patch paths must be absolute.
# Never silently continue with unpatched upstream source.
PATCH_DIR=$(realpath "$(dirname "${BASH_SOURCE[0]}")/patches")
for proj in SDL2 FAudio dxvk; do
    for p in "$PATCH_DIR/${proj,,}"-*.patch; do
        [ -e "$p" ] || continue
        echo "applying $(basename "$p") to $proj"
        git -C "$SRC/$proj" apply --check "$p"
        git -C "$SRC/$proj" apply "$p"
    done
done

CMAKE_ANDROID=(-G Ninja -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI="$ABI"
               -DANDROID_PLATFORM=android-$API -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX"
               -DCMAKE_PREFIX_PATH="$PREFIX" -DCMAKE_FIND_ROOT_PATH="$PREFIX")

# SDL2 (also installs SDL2.pc, which DXVK's meson build looks up).
cmake -S "$SRC/SDL2" -B "$SRC/build/SDL2-$ABI" "${CMAKE_ANDROID[@]}" -DSDL_STATIC=OFF -DSDL_TEST=OFF
cmake --build "$SRC/build/SDL2-$ABI" -j "$JOBS"
cmake --install "$SRC/build/SDL2-$ABI"

# FAudio on SDL2.
cmake -S "$SRC/FAudio" -B "$SRC/build/FAudio-$ABI" "${CMAKE_ANDROID[@]}" -DBUILD_SDL3=OFF -DBUILD_SHARED_LIBS=ON \
      -DSDL2_DIR="$PREFIX/lib/cmake/SDL2"
cmake --build "$SRC/build/FAudio-$ABI" -j "$JOBS"
cmake --install "$SRC/build/FAudio-$ABI"

# DXVK native, d3d9 only.
TC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin
CROSS=$SRC/build/android-$ABI.cross
cat > "$CROSS" <<EOF
[binaries]
c = '$TC/$TRIPLE$API-clang'
cpp = '$TC/$TRIPLE$API-clang++'
ar = '$TC/llvm-ar'
strip = '$TC/llvm-strip'
pkg-config = 'pkg-config'

[properties]
pkg_config_libdir = '$PREFIX/lib/pkgconfig'
needs_exe_wrapper = true

[host_machine]
system = 'android'
cpu_family = '$CPU_FAMILY'
cpu = '$CPU'
endian = 'little'

[built-in options]
# Meson must not silently turn this into a desktop Unix build. DXVK uses
# __ANDROID__ to select libSDL2.so, libvulkan.so and Android surface WSI.
c_args = ['-D__ANDROID__=1']
cpp_args = ['-D__ANDROID__=1']
EOF
rm -rf "$SRC/build/dxvk-$ABI"
meson setup "$SRC/build/dxvk-$ABI" "$SRC/dxvk" --cross-file "$CROSS" --buildtype release --prefix "$PREFIX" \
      -Denable_d3d9=true -Denable_d3d8=false -Denable_d3d10=false -Denable_d3d11=false -Denable_dxgi=false \
      -Ddxvk_native_wsi=sdl2
ninja -C "$SRC/build/dxvk-$ABI" -j "$JOBS"
ninja -C "$SRC/build/dxvk-$ABI" install

# Fail the build rather than shipping a desktop-WSI DXVK by accident. This
# checks the installed library, not just the compiler or a successful link.
DXVK_LIB="$PREFIX/lib/libdxvk_d3d9.so"
if [ ! -f "$DXVK_LIB" ]; then
    echo "missing installed DXVK library: $DXVK_LIB" >&2
    exit 1
fi
DXVK_STRINGS=$($TC/llvm-strings "$DXVK_LIB")
printf '%s\n' "$DXVK_STRINGS" | grep -E 'libSDL|vkCreateAndroidSurface|SDL2 WSI' || true
printf '%s\n' "$DXVK_STRINGS" | grep -Fx 'libSDL2.so' >/dev/null || {
    echo "DXVK was not compiled with the Android SDL2 soname" >&2
    exit 1
}
if printf '%s\n' "$DXVK_STRINGS" | grep -Fx 'libSDL2-2.0.so.0' >/dev/null; then
    echo "DXVK contains the desktop SDL2 soname" >&2
    exit 1
fi
printf '%s\n' "$DXVK_STRINGS" | grep -F 'vkCreateAndroidSurfaceKHR' >/dev/null || {
    echo "DXVK was not compiled with Android surface support" >&2
    exit 1
}

echo "installed to $PREFIX:"
find "$PREFIX" -name "*.so" | sort
