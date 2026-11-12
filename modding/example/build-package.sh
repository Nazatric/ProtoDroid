#!/bin/bash
# Build the source example with an Android NDK and create an importable ZIP. No prebuilt plugin is stored in Git.
set -euo pipefail
ROOT=$(realpath "$(dirname "${BASH_SOURCE[0]}")/../..")
NDK=${ANDROID_NDK_HOME:-${ANDROID_NDK_LATEST_HOME:?set ANDROID_NDK_HOME}}
BUILD=${1:-$ROOT/build/mod-example}
cmake -S "$ROOT/modding/example" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD"
rm -rf "$ROOT/modding/example/package/lib"
mkdir -p "$ROOT/modding/example/package/lib/arm64-v8a"
cp "$BUILD/libprotodroid_example.so" "$ROOT/modding/example/package/lib/arm64-v8a/"
python3 - "$ROOT" <<'PY'
from pathlib import Path
import zipfile
import sys
root = Path(sys.argv[1])
package = root / "modding/example/package"
out = root / "build/protodroid-example-arm64.zip"
out.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for path in sorted(package.rglob("*")):
        if path.is_file():
            z.write(path, path.relative_to(package).as_posix())
print(out)
PY
