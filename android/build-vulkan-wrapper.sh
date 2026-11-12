#!/bin/bash
# Build GameNative/Mesa's Vulkan compatibility wrapper as a real Android ARM64 ICD for ProtoDroid.
# This is intentionally not GameNative's Termux/X11 build: the APK uses Android WSI, no Termux sysroot, and no
# adrenotools/custom-driver path. The resulting library is still only a candidate until validated on physical GPUs.
#
# Usage: android/build-vulkan-wrapper.sh <arm64-v8a> <source dir> <install prefix>
# Needs ANDROID_NDK_HOME (or ANDROID_NDK_LATEST_HOME), meson, ninja, git, Python, mako, packaging and PyYAML.
set -euo pipefail

ABI=${1:?abi}
SRC=$(realpath -m "${2:?source dir}")
PREFIX=$(realpath -m "${3:?install prefix}")
NDK=${ANDROID_NDK_HOME:-${ANDROID_NDK_LATEST_HOME:?set ANDROID_NDK_HOME}}
API=26
JOBS=${JOBS:-$(nproc)}
MESA_COMMIT=6bcfbaa701a8d1efa7f6c54145495eda5329f767
PATCH=$(realpath "$(dirname "${BASH_SOURCE[0]}")/patches/mesa-protodroid-android-wrapper.patch")

if [ "$ABI" != arm64-v8a ]; then
    echo "the compatibility wrapper candidate is currently built only for arm64-v8a" >&2
    exit 2
fi
TRIPLE=aarch64-linux-android
TC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin
MESA=$SRC/mesa-wrapper
mkdir -p "$SRC" "$PREFIX/lib"

# Exact-commit fetch, not a release asset or a moving branch/tag. This source directory is a dependency copy, never
# the ProtoDroid worktree. Rebuilding resets only that copy before applying ProtoDroid's small Android/APK patch.
if [ ! -d "$MESA/.git" ]; then
    git init -q "$MESA"
    git -C "$MESA" remote add origin https://github.com/GameNative/mesa.git
fi
if ! git -C "$MESA" cat-file -e "$MESA_COMMIT^{commit}" 2>/dev/null; then
    git -C "$MESA" fetch -q --depth 1 origin "$MESA_COMMIT"
fi
git -C "$MESA" checkout -q --detach "$MESA_COMMIT"
git -C "$MESA" reset -q --hard "$MESA_COMMIT"
test "$(git -C "$MESA" rev-parse HEAD)" = "$MESA_COMMIT"
git -C "$MESA" apply --check "$PATCH"
git -C "$MESA" apply "$PATCH"

CROSS=$SRC/build/protodroid-wrapper-$ABI.cross
mkdir -p "$(dirname "$CROSS")"
cat > "$CROSS" <<EOF
[binaries]
c = '$TC/$TRIPLE$API-clang'
cpp = '$TC/$TRIPLE$API-clang++'
ar = '$TC/llvm-ar'
strip = '$TC/llvm-strip'
pkg-config = 'pkg-config'

[properties]
needs_exe_wrapper = true

[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'armv8-a'
endian = 'little'

[built-in options]
# Keep __ANDROID__ defined. In particular, do not copy GameNative's -U__ANDROID__/__TERMUX__/X11 settings.
c_args = ['-D__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__', '-D__USE_GNU', '-include', 'fcntl.h', '-include', 'unistd.h']
cpp_args = ['-D__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__', '-D__USE_GNU', '-include', 'fcntl.h', '-include', 'unistd.h']
cpp_link_args = ['-static-libstdc++']
EOF

BUILD=$SRC/build/mesa-wrapper-$ABI
rm -rf "$BUILD"
# Cross dependency discovery must never resolve desktop x86 packages. With no host pkg-config entries available,
# Meson can use the NDK system zlib or Mesa's pinned fallback; optional zstd is disabled below.
PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig" \
meson setup "$BUILD" "$MESA" --cross-file "$CROSS" --buildtype release --prefix "$PREFIX" \
    -Dcpp_rtti=false \
    -Dgbm=disabled \
    -Dopengl=false \
    -Dllvm=disabled \
    -Dshared-llvm=disabled \
    -Dplatforms=android \
    -Dplatform-sdk-version=$API \
    -Dandroid-libbacktrace=disabled \
    -Dgallium-drivers= \
    -Dxmlconfig=disabled \
    -Dzstd=disabled \
    -Dvulkan-drivers=wrapper
ninja -C "$BUILD" -j "$JOBS" src/vulkan/wrapper/libvulkan_wrapper.so

WRAPPER=$BUILD/src/vulkan/wrapper/libvulkan_wrapper.so
test -s "$WRAPPER"
cp "$WRAPPER" "$PREFIX/lib/libvulkan_wrapper.so"
INSTALLED=$PREFIX/lib/libvulkan_wrapper.so

# Verify the actual Android ARM64 artifact, not merely a successful Meson invocation.
"$TC/llvm-readelf" -h "$INSTALLED" | grep -E 'Class:|Machine:'
"$TC/llvm-readelf" -h "$INSTALLED" | grep -F 'ELF64' >/dev/null
"$TC/llvm-readelf" -h "$INSTALLED" | grep -F 'AArch64' >/dev/null
"$TC/llvm-nm" -D "$INSTALLED" | grep -E '[[:space:]]vk_icdGetInstanceProcAddr$' >/dev/null || {
    echo "wrapper does not export vk_icdGetInstanceProcAddr" >&2
    exit 1
}
STRINGS=$($TC/llvm-strings "$INSTALLED")
printf '%s\n' "$STRINGS" | grep -F 'vkCreateAndroidSurfaceKHR' >/dev/null || {
    echo "wrapper lacks Android surface WSI" >&2
    exit 1
}
"$TC/llvm-nm" "$INSTALLED" | grep -E '[[:space:]]wsi_configure_android_image$' >/dev/null || {
    echo "wrapper lacks the linked Android WSI image implementation" >&2
    exit 1
}
if "$TC/llvm-nm" "$INSTALLED" | grep -E '[[:space:]]wsi_x11_init_wsi$' >/dev/null; then
    echo "wrapper contains the X11 WSI implementation" >&2
    exit 1
fi
printf '%s\n' "$STRINGS" | grep -F '/system/lib64/libvulkan.so' >/dev/null || {
    echo "wrapper does not contain the Android system-loader path" >&2
    exit 1
}
printf '%s\n' "$STRINGS" | grep -F '/storage/emulated/0/Prototype/logs/vulkan-wrapper' >/dev/null || {
    echo "wrapper does not contain ProtoDroid's diagnostic path" >&2
    exit 1
}
printf '%s\n' "$STRINGS" | grep -F 'ProtoDroid DXVK instance request' >/dev/null || {
    echo "wrapper lacks the DXVK effective-instance/base-loader negotiation adaptation" >&2
    exit 1
}
printf '%s\n' "$STRINGS" | grep -F 'WRAPPER_WORK_DIR' >/dev/null || {
    echo "wrapper auxiliary logs do not honor ProtoDroid's isolated directory" >&2
    exit 1
}
printf '%s\n' "$STRINGS" | grep -F '/storage/emulated/0/Android/data/com.protodroid/files/vulkan-wrapper-cache' >/dev/null || {
    echo "wrapper lacks ProtoDroid's app-specific cache fallback" >&2
    exit 1
}
# Vulkan XML generation may retain Xcb/Xlib API names without compiling those WSIs. Reject actual implementation
# references and dependencies instead; the X11 implementation symbol and DT_NEEDED checks remain independent.
for forbidden in xcb_connect XOpenDisplay XCloseDisplay adrenotools_open_libvulkan /data/data/com.termux /data/data/com.winlator.cmod /data/data/app.gamenative /sdcard/wrapper; do
    if printf '%s\n' "$STRINGS" | grep -F "$forbidden" >/dev/null; then
        echo "wrapper contains forbidden Termux/X11/adrenotools marker: $forbidden" >&2
        exit 1
    fi
done
NEEDED=$("$TC/llvm-readelf" -d "$INSTALLED" | grep -F '(NEEDED)' || true)
printf '%s\n' "$NEEDED"
for forbidden in libX11 libxcb libadrenotools libc++_shared; do
    if printf '%s\n' "$NEEDED" | grep -F "$forbidden" >/dev/null; then
        echo "wrapper has an unpackaged/unsupported dependency: $forbidden" >&2
        exit 1
    fi
done

echo "built $INSTALLED from GameNative/Mesa $MESA_COMMIT with Android WSI"
ls -lh "$INSTALLED"
