#!/bin/bash
# Packages the original native Android launcher without Gradle: SDL's matched Java activity plus ProtoDroid's Java
# views become classes.dex, platform assets/manifest become the base APK, and verified native libraries are staged,
# zipaligned and signed. KEYSTORE and its password/alias environment variables select the signing identity.
#
# Usage: android/build-apk.sh [abi ...]          (default: arm64-v8a)
# Needs: build/android/<abi>/libmain.so (make -C src/native PLATFORM=android ABI=<abi>) and the libraries from
# android/build-deps.sh in $DEPS_ROOT/<abi>/lib. Output: build/apk/prototype.apk
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SDK=${ANDROID_HOME:-$HOME/Android/Sdk}
BUILD_TOOLS=${BUILD_TOOLS:-$SDK/build-tools/36.0.0}
PLATFORM_JAR=${PLATFORM_JAR:-$SDK/platforms/android-35/android.jar}
DEPS_ROOT=${DEPS_ROOT:-$HOME/protodroid-data/android-deps}
TURNIP_ROOT=${TURNIP_ROOT:-$HOME/protodroid-data/android-turnip}
SDL_SRC=${SDL_SRC:-$HOME/protodroid-data/android-deps-src/SDL2}
KEYSTORE=${KEYSTORE:-$HOME/.android/debug.keystore}
KEYSTORE_PASS=${KEYSTORE_PASS:-android}
KEY_ALIAS=${KEY_ALIAS:-androiddebugkey}
KEY_PASS=${KEY_PASS:-$KEYSTORE_PASS}
SIGNING_IDENTITY=${SIGNING_IDENTITY:-local}
export KEYSTORE_PASS KEY_PASS
case "$SIGNING_IDENTITY" in
    local|ephemeral|persistent) ;;
    *) echo "ERROR: invalid SIGNING_IDENTITY=$SIGNING_IDENTITY"; exit 1 ;;
esac
ABIS=("${@:-arm64-v8a}")

if [ ! -s "$ROOT/android/java/com/protodroid/SetupView.java" ]; then
    echo "ERROR: original native Android launcher source is missing"
    exit 1
fi

OUT=$ROOT/build/apk
rm -rf "$OUT"
mkdir -p "$OUT/classes" "$OUT/dex" "$OUT/pkg"
VERSION_INFO=$(python3 "$ROOT/tools/bump_version.py" --show "$ROOT/android/AndroidManifest.xml")
read -r VERSION_NAME VERSION_CODE <<< "$VERSION_INFO"
SDL2_VERSION=$(python3 "$ROOT/tools/sdl_version.py" pin "$ROOT/android/build-deps.sh")
echo "=== Package version: $VERSION_NAME (versionCode $VERSION_CODE); SDL $SDL2_VERSION ==="

# Compile the exact pinned SDL Java source and the original native Android views. Verify both the source constants and
# the resulting SDLActivity class so a second SDL version cannot enter the DEX through a reused checkout or cache.
python3 "$ROOT/tools/sdl_version.py" source "$SDL_SRC" "$SDL2_VERSION"
javac --release 11 -nowarn -cp "$PLATFORM_JAR" -d "$OUT/classes" \
    "$SDL_SRC"/android-project/app/src/main/java/org/libsdl/app/*.java "$ROOT"/android/java/com/protodroid/*.java
javap -classpath "$OUT/classes" -private -verbose org.libsdl.app.SDLActivity > "$OUT/sdl-activity.javap"
python3 "$ROOT/tools/sdl_version.py" verify-class "$OUT/sdl-activity.javap" "$SDL2_VERSION"
"$BUILD_TOOLS/d8" --release --min-api 26 --lib "$PLATFORM_JAR" --output "$OUT/dex" \
    $(find "$OUT/classes" -name '*.class')

"$BUILD_TOOLS/aapt2" compile --dir "$ROOT/android/res" -o "$OUT/res.zip"
"$BUILD_TOOLS/aapt2" link -o "$OUT/base.apk" -I "$PLATFORM_JAR" --manifest "$ROOT/android/AndroidManifest.xml" \
    -A "$ROOT/android/assets" -R "$OUT/res.zip" --auto-add-overlay --min-sdk-version 26 --target-sdk-version 35
cp "$OUT/dex/classes.dex" "$OUT/pkg/"
echo "=== classes.dex size ==="
ls -lh "$OUT/dex/classes.dex" "$OUT/pkg/classes.dex"
for abi in "${ABIS[@]}"; do
    mkdir -p "$OUT/pkg/lib/$abi"
    echo "=== Bundling libs for $abi ==="
    echo "DEPS_ROOT=$DEPS_ROOT ABI=$abi ROOT=$ROOT"
    ls -lh "$ROOT/build/android/$abi/libmain.so" 2>&1 || echo "MISSING libmain.so at $ROOT/build/android/$abi/libmain.so"
    ls -lh "$DEPS_ROOT/$abi/lib/libSDL2.so" "$DEPS_ROOT/$abi/lib/libFAudio.so" "$DEPS_ROOT/$abi/lib/libdxvk_d3d9.so" 2>&1 || echo "MISSING deps for $abi in $DEPS_ROOT/$abi/lib/"
    # Explicitly copy each lib with verification
    for lib in libmain.so libSDL2.so libFAudio.so libdxvk_d3d9.so; do
        SRC=""
        if [ "$lib" = "libmain.so" ]; then SRC="$ROOT/build/android/$abi/libmain.so"; else SRC="$DEPS_ROOT/$abi/lib/$lib"; fi
        if [ ! -f "$SRC" ]; then echo "ERROR: missing $SRC"; ls -l "$SRC" 2>&1; echo "DEPS lib dir:"; ls -lh "$DEPS_ROOT/$abi/lib/" 2>&1 | head -n 20; exit 1; fi
        if [ "$lib" = libSDL2.so ]; then
            python3 "$ROOT/tools/sdl_version.py" verify-manifest \
                "$DEPS_ROOT/$abi/lib/protodroid-sdl2-version.txt" "$SRC" "$SDL2_VERSION"
        fi
        echo "Copying $SRC -> $OUT/pkg/lib/$abi/"
        cp -v "$SRC" "$OUT/pkg/lib/$abi/"
        ls -lh "$OUT/pkg/lib/$abi/$lib"
    done
    if [ "$abi" = arm64-v8a ]; then
        if [ ! -s "$DEPS_ROOT/$abi/lib/libvulkan_wrapper.so" ]; then
            echo "ERROR: missing Android-WSI Vulkan compatibility wrapper; rebuild the pinned dependency batch"
            exit 1
        fi
        cp -v "$DEPS_ROOT/$abi/lib/libvulkan_wrapper.so" "$OUT/pkg/lib/$abi/"
        if [ ! -s "$ROOT/build/android/$abi/libturnipprobe.so" ]; then
            echo "ERROR: missing ARM64 isolated Vulkan driver probe; rebuild with TURNIP_ROOT=$TURNIP_ROOT"
            exit 1
        fi
        cp -v "$ROOT/build/android/$abi/libturnipprobe.so" "$OUT/pkg/lib/$abi/"
    elif [ -e "$ROOT/build/android/$abi/libturnipprobe.so" ]; then
        cp -v "$ROOT/build/android/$abi/libturnipprobe.so" "$OUT/pkg/lib/$abi/"
    fi
    if [ -e "$ROOT/build/android/$abi-v80/libmain.so" ]; then
        echo "Copying libmain_v80.so"
        cp -v "$ROOT/build/android/$abi-v80/libmain.so" "$OUT/pkg/lib/$abi/libmain_v80.so"
        ls -lh "$OUT/pkg/lib/$abi/libmain_v80.so"
    fi
    if [ "$abi" = arm64-v8a ]; then
        echo "Copying and verifying Turnip/AdrenoTools libs"
        for library in libvulkan_freedreno.so libvulkan_freedreno_next.so libvulkan_freedreno_gen8.so \
                libhook_impl.so libmain_hook.so libfile_redirect_hook.so libgsl_alloc_hook.so; do
            if [ ! -s "$TURNIP_ROOT/lib/$library" ]; then
                echo "ERROR: missing required Vulkan driver library $TURNIP_ROOT/lib/$library"
                exit 1
            fi
        done
        cp -v "$TURNIP_ROOT"/lib/*.so "$OUT/pkg/lib/$abi/"
    fi
    echo "=== Before strip ==="
    ls -lh "$OUT/pkg/lib/$abi/"
    du -sh "$OUT/pkg/lib/$abi/"
    "$SDK"/ndk/*/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip --strip-unneeded "$OUT/pkg/lib/$abi"/*.so || echo "strip failed or no files"
    echo "=== After strip ==="
    ls -lh "$OUT/pkg/lib/$abi/"
    du -sh "$OUT/pkg/lib/$abi/"
done
echo "=== Packaging base.apk ==="
du -sh "$OUT/pkg"/lib/*/* 2>&1 | head -n 20
# Native code dominates this package. Maximum DEFLATE costs build time only; every signed-entry verification below
# still compares the extracted bytes with the exact compiled/staged inputs.
(cd "$OUT/pkg" && zip -9 -q -r "$OUT/base.apk" classes.dex lib && echo "zip done (DEFLATE level 9)" && ls -lh "$OUT/base.apk" && unzip -l "$OUT/base.apk" | head -n 100)

"$BUILD_TOOLS/zipalign" -P 16 -f 4 "$OUT/base.apk" "$OUT/aligned.apk"
"$BUILD_TOOLS/apksigner" sign --ks "$KEYSTORE" --ks-key-alias "$KEY_ALIAS" \
    --ks-pass env:KEYSTORE_PASS --key-pass env:KEY_PASS --out "$OUT/prototype.apk" "$OUT/aligned.apk"
"$BUILD_TOOLS/apksigner" verify --verbose --print-certs "$OUT/prototype.apk" | tee "$OUT/signature.txt"
CERTIFICATE_SHA256=$(awk 'index($0, "certificate SHA-256 digest:") { sub(/^.*certificate SHA-256 digest:[[:space:]]*/, ""); gsub(/[[:space:]]/, ""); print; exit }' "$OUT/signature.txt" | tr 'A-F' 'a-f')
if ! printf '%s' "$CERTIFICATE_SHA256" | grep -Eq '^[0-9a-f]{64}$'; then
    echo "ERROR: could not verify the signed APK certificate digest"
    exit 1
fi
printf 'identity=%s\ncertificateSha256=%s\n' "$SIGNING_IDENTITY" "$CERTIFICATE_SHA256" > "$OUT/signing.txt"
"$BUILD_TOOLS/zipalign" -c -P 16 4 "$OUT/prototype.apk"
# The redesigned setup must be fully offline. Compare every packaged setup resource byte-for-byte with its source so
# an APK can never silently fall back to missing artwork or platform fonts.
python3 - "$OUT/prototype.apk" "$ROOT/android/assets" <<'PY'
from pathlib import Path
import sys
import zipfile

apk, asset_root = sys.argv[1], Path(sys.argv[2])
expected = sorted(p for p in asset_root.rglob("*") if p.is_file())
if not expected:
    raise SystemExit("ERROR: no Android assets found")
with zipfile.ZipFile(apk) as archive:
    names = set(archive.namelist())
    for source in expected:
        name = "assets/" + source.relative_to(asset_root).as_posix()
        if name not in names:
            raise SystemExit(f"ERROR: signed APK is missing {name}")
        if archive.read(name) != source.read_bytes():
            raise SystemExit(f"ERROR: signed APK asset differs from source: {name}")
        print(f"Verified {name} ({source.stat().st_size} bytes)")
PY
# The signed package must contain the original native Android launcher and the exact SDL-verified DEX/native bytes.
python3 - "$OUT/prototype.apk" "$OUT/dex/classes.dex" "$OUT/pkg" "$SDL2_VERSION" "${ABIS[@]}" <<'PY'
from pathlib import Path
import sys
import zipfile

apk_path, dex_path, pkg_root, sdl_version, *abis = sys.argv[1:]
with zipfile.ZipFile(apk_path) as apk:
    names = set(apk.namelist())
    if any(name.startswith("assets/launcher/") for name in names):
        raise SystemExit("ERROR: signed APK contains the retired Web launcher")
    dex_names = sorted(name for name in names if name.startswith("classes") and name.endswith(".dex"))
    if not dex_names:
        raise SystemExit("ERROR: signed APK has no DEX bytecode")
    if apk.read("classes.dex") != Path(dex_path).read_bytes():
        raise SystemExit("ERROR: signed DEX differs from the SDL-verified compiled classes")
    dex = b"".join(apk.read(name) for name in dex_names)
    for marker in (b"SetupView", b"SetupActivity", b"ProtoActivity"):
        if marker not in dex:
            raise SystemExit("ERROR: signed APK lacks original native launcher marker " + marker.decode())
    for forbidden in (b"LauncherAppKt", b"ProtoDroidLauncherHost", b"PreviewLauncherHost", b"addJavascriptInterface"):
        if forbidden in dex:
            raise SystemExit("ERROR: signed APK contains rejected Compose/Web marker " + forbidden.decode())
    for abi in abis:
        name = f"lib/{abi}/libSDL2.so"
        staged = Path(pkg_root) / "lib" / abi / "libSDL2.so"
        if name not in names or apk.read(name) != staged.read_bytes():
            raise SystemExit(f"ERROR: signed {name} differs from the provenance-verified staged library")
    print(f"Verified original native launcher and signed SDL {sdl_version} Java/native package chain for {', '.join(abis)}")
PY
# The A825 fix is part of the native package, not an optional download. Verify it in the signed APK itself.
if [[ " ${ABIS[*]} " == *" arm64-v8a "* ]]; then
python3 - "$OUT/prototype.apk" "$OUT/pkg/lib/arm64-v8a/libvulkan_wrapper.so" <<'PY'
import sys
import zipfile
from pathlib import Path
required = (
    "libvulkan_freedreno.so", "libvulkan_freedreno_next.so", "libvulkan_freedreno_gen8.so",
    "libturnipprobe.so", "libvulkan_wrapper.so", "libhook_impl.so", "libmain_hook.so",
    "libfile_redirect_hook.so", "libgsl_alloc_hook.so",
)
with zipfile.ZipFile(sys.argv[1]) as apk:
    names = set(apk.namelist())
    for library in required:
        path = "lib/arm64-v8a/" + library
        if path not in names or apk.getinfo(path).file_size == 0:
            raise SystemExit("ERROR: signed APK is missing " + path)
        print("Verified " + path)
    # Both the isolated probe and gameplay must contain the direct-ICD path. These dlsym names survive stripping;
    # checking only Java metadata or a successful link would not prove that the signed package contains the loader.
    for library in ("libmain.so", "libturnipprobe.so", "libmain_v80.so"):
        path = "lib/arm64-v8a/" + library
        if path not in names:
            if library == "libmain_v80.so":
                continue
            raise SystemExit("ERROR: signed APK is missing native driver consumer " + path)
        data = apk.read(path)
        for marker in (b"vk_icdGetInstanceProcAddr", b"vk_icdNegotiateLoaderICDInterfaceVersion"):
            if marker not in data:
                raise SystemExit("ERROR: " + path + " lacks direct ICD marker " + marker.decode())
        print("Verified direct ICD loader in " + path)

    wrapper_path = "lib/arm64-v8a/libvulkan_wrapper.so"
    wrapper = apk.read(wrapper_path)
    if wrapper != Path(sys.argv[2]).read_bytes():
        raise SystemExit("ERROR: signed Android wrapper differs from the staged verified artifact")
    for marker in (b"vk_icdGetInstanceProcAddr", b"vkCreateAndroidSurfaceKHR", b"/system/lib64/libvulkan.so",
                   b"ProtoDroid DXVK instance request", b"WRAPPER_WORK_DIR",
                   b"/storage/emulated/0/Android/data/com.protodroid/files/vulkan-wrapper-cache"):
        if marker not in wrapper:
            raise SystemExit("ERROR: signed Android wrapper lacks marker " + marker.decode())
    # Generated Vulkan API names are not proof of X11 linkage. The build script checks WSI symbols and DT_NEEDED;
    # this signed-package check compares exact staged bytes and rejects actual X11/reference and Termux markers.
    for forbidden in (b"xcb_connect", b"XOpenDisplay", b"XCloseDisplay", b"adrenotools_open_libvulkan",
                      b"/data/data/com.termux", b"/data/data/com.winlator.cmod",
                      b"/data/data/app.gamenative", b"/sdcard/wrapper"):
        if forbidden in wrapper:
            raise SystemExit("ERROR: signed Android wrapper contains forbidden marker " + forbidden.decode())
    print("Verified Android-WSI system compatibility wrapper")

    for library in ("libmain.so", "libturnipprobe.so", "libmain_v80.so"):
        path = "lib/arm64-v8a/" + library
        if path not in names:
            if library == "libmain_v80.so":
                continue
            raise SystemExit("ERROR: signed APK is missing wrapper consumer " + path)
        data = apk.read(path)
        for marker in (b"DXVK", b"nullDescriptor", b"vkGetPhysicalDeviceFeatures2"):
            if marker not in data:
                raise SystemExit("ERROR: " + path + " lacks effective-wrapper capability marker " + marker.decode())
        print("Verified wrapper capability probe in " + path)
PY
fi
# Verify the metadata in the signed APK, not just the source manifest. The release job consumes this only after the
# exact package assertion succeeds. versionName is visible metadata; versionCode is Android's monotonic update ID.
"$BUILD_TOOLS/aapt2" dump badging "$OUT/prototype.apk" > "$OUT/badging.txt"
head -n 5 "$OUT/badging.txt"
EXPECTED_PACKAGE="package: name='com.protodroid' versionCode='$VERSION_CODE' versionName='$VERSION_NAME'"
if ! grep -F -- "$EXPECTED_PACKAGE" "$OUT/badging.txt"; then
    echo "ERROR: signed APK package/version mismatch; expected $EXPECTED_PACKAGE"
    exit 1
fi
printf 'versionName=%s\nversionCode=%s\n' "$VERSION_NAME" "$VERSION_CODE" > "$OUT/version.txt"
if [ -f "$ROOT/build/native-build.txt" ]; then
    cp "$ROOT/build/native-build.txt" "$OUT/native-build.txt"
else
    printf 'nativeBuild=unknown\nsourceRevision=unknown\n' > "$OUT/native-build.txt"
fi
echo "=== Final APK ==="
ls -lh "$OUT/prototype.apk"
stat -c%s "$OUT/prototype.apk" | awk '{printf "APK size bytes: %d (MB %.2f)\n", $1, $1/1024/1024}'
# Verify size is at least 20MB (real APK should be 35-50MB, dummy is 2MB)
APK_SIZE=$(stat -c%s "$OUT/prototype.apk")
if [ "$APK_SIZE" -lt 20000000 ]; then
    echo "ERROR: APK too small ($APK_SIZE bytes) - expected 35-50MB, missing libs!"
    echo "Contents:"
    unzip -l "$OUT/prototype.apk" | head -n 100
    echo "lib dir:"
    unzip -l "$OUT/prototype.apk" | grep "lib/"
    exit 1
fi
echo "APK OK: $APK_SIZE bytes"
APK_SHA256=$(sha256sum "$OUT/prototype.apk" | awk '{print $1}')
printf 'apkBytes=%s\napkSha256=%s\n' "$APK_SIZE" "$APK_SHA256" > "$OUT/package.txt"
unzip -l "$OUT/prototype.apk" | grep -E "libSDL2|libFAudio|libdxvk|libmain|classes.dex"
