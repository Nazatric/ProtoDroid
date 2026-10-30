#!/bin/bash
# Packages the APK without Gradle: SDL's Java activity + ours -> classes.dex, the manifest -> aapt2, native
# libraries for the requested ABIs, then zipalign and apksigner (debug key).
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
ABIS=("${@:-arm64-v8a}")

OUT=$ROOT/build/apk
rm -rf "$OUT"
mkdir -p "$OUT/classes" "$OUT/dex" "$OUT/pkg"
VERSION_INFO=$(python3 "$ROOT/tools/bump_version.py" --show "$ROOT/android/AndroidManifest.xml")
read -r VERSION_NAME VERSION_CODE <<< "$VERSION_INFO"
echo "=== Package version: $VERSION_NAME (versionCode $VERSION_CODE) ==="

# Java: SDL's activity classes plus ProtoActivity.
javac --release 11 -nowarn -cp "$PLATFORM_JAR" -d "$OUT/classes" \
    "$SDL_SRC"/android-project/app/src/main/java/org/libsdl/app/*.java "$ROOT"/android/java/com/protodroid/*.java
"$BUILD_TOOLS/d8" --release --min-api 26 --lib "$PLATFORM_JAR" --output "$OUT/dex" \
    $(find "$OUT/classes" -name '*.class')

# Resources (launcher icon; tools/make_icon.py draws it) and the self-contained setup artwork/fonts.
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
        echo "Copying $SRC -> $OUT/pkg/lib/$abi/"
        cp -v "$SRC" "$OUT/pkg/lib/$abi/"
        ls -lh "$OUT/pkg/lib/$abi/$lib"
    done
    if [ -e "$ROOT/build/android/$abi/libturnipprobe.so" ]; then
        echo "Copying libturnipprobe.so"
        cp -v "$ROOT/build/android/$abi/libturnipprobe.so" "$OUT/pkg/lib/$abi/"
    fi
    if [ -e "$ROOT/build/android/$abi-v80/libmain.so" ]; then
        echo "Copying libmain_v80.so"
        cp -v "$ROOT/build/android/$abi-v80/libmain.so" "$OUT/pkg/lib/$abi/libmain_v80.so"
        ls -lh "$OUT/pkg/lib/$abi/libmain_v80.so"
    fi
    if [ "$abi" = arm64-v8a ] && [ -e "$TURNIP_ROOT/lib/libvulkan_freedreno.so" ]; then
        echo "Copying Turnip libs"
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
(cd "$OUT/pkg" && zip -q -r "$OUT/base.apk" classes.dex lib && echo "zip done" && ls -lh "$OUT/base.apk" && unzip -l "$OUT/base.apk" | head -n 100)

"$BUILD_TOOLS/zipalign" -P 16 -f 4 "$OUT/base.apk" "$OUT/aligned.apk"
"$BUILD_TOOLS/apksigner" sign --ks "$KEYSTORE" --ks-pass pass:android --key-pass pass:android \
    --out "$OUT/prototype.apk" "$OUT/aligned.apk"
"$BUILD_TOOLS/apksigner" verify --verbose --print-certs "$OUT/prototype.apk"
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
unzip -l "$OUT/prototype.apk" | grep -E "libSDL2|libFAudio|libdxvk|libmain|classes.dex"
