#!/bin/bash
# Build protodroid-v0.16-latest (versionCode 33): the 0.17.3 base + the proven sync layer +
# useD32forD24 + the perf13 sync/drop work (immortal kernel objects, display-mode pin) +
# the shipped minimap mod with the in-game MODS panel. Libraries: the original release .so
# files for everything except libmain.
set -u
DATA=$HOME/protodroid-data
ROOT=/home/z/my-project/work/v1
LOG=/tmp/build_v016.log
: > $LOG
step() { echo "[$(date +%H:%M:%S)] $1" >> $LOG; }

export JAVA_HOME=$DATA/jdk
export PATH=$JAVA_HOME/bin:$HOME/.venv/bin:$PATH
export ANDROID_HOME=$DATA/android-sdk
export DEPS_ROOT=$DATA/android-deps
export SDL_SRC=$DATA/deps-src/SDL2
export BUILD_TOOLS=$DATA/android-sdk/build-tools/36.0.0
export PLATFORM_JAR=$DATA/android-sdk/platforms/android-35/android.jar
export ANDROID_NDK_HOME=$DATA/ndk

# 1. libmain: 0.17.3 source + the ported sync layer + the D32 bake + the perf work
cd $ROOT/src/native
make PLATFORM=android ABI=arm64-v8a -j2 NDK=$DATA/ndk DXVK=$DATA/deps-src/dxvk \
     DEPS=$DATA/android-deps/arm64-v8a \
     SOURCE_REVISION="3fa9f575c7e8aa076cb84b742d9d568e7d4282fb+syncfix+useD32forD24+perf14+minimap" >> $LOG 2>&1 || { step "FATAL libmain build"; tail -25 $LOG; exit 1; }
LIBMAIN=$ROOT/build/android/arm64-v8a/libmain.so
[ -s "$LIBMAIN" ] || { step "FATAL no libmain output"; exit 1; }
step "libmain built: $(stat -c%s $LIBMAIN) bytes"

# 2. the fixes must be present: the stamp in the binary, the sync contract and the immortal
#    handle table at source level, the minimap package staged into the APK assets
strings -a "$LIBMAIN" | grep -q "useD32forD24" || { step "FATAL useD32forD24 missing"; exit 1; }
strings -a "$LIBMAIN" | grep -q "+perf14+minimap" || { step "FATAL perf14+minimap stamp missing"; exit 1; }
grep -qi "objects are immortal" $ROOT/src/native/kernel32.cpp || { step "FATAL immortal handle table missing"; exit 1; }
grep -qi "tokens live in the atomic count" $ROOT/src/native/sync_futex.h || { step "FATAL ported sync layer missing"; exit 1; }
[ -s "$ROOT/android/assets/mods/protodroid-minimap.zip" ] || { step "FATAL minimap package missing"; exit 1; }
step "fixes verified"

# 3. the original release libraries are staged in $DEPS_ROOT (verified byte-identical to the
#    release bytes below, after packaging)

# 4. keystore (survives from the previous round; regenerate if missing)
mkdir -p $HOME/.android
if [ ! -s $HOME/.android/debug.keystore ]; then
    keytool -genkeypair -v -keystore $HOME/.android/debug.keystore -alias androiddebugkey \
        -storepass android -keypass android -keyalg RSA -keysize 2048 -validity 10000 \
        -dname "CN=Android Debug,O=Android,C=US" >> $LOG 2>&1 || { step "FATAL keytool"; exit 1; }
    step "keystore generated"
fi

# 5. package + sign
if bash $ROOT/android/build-apk.sh arm64-v8a >> $LOG 2>&1; then
    step "apk OK"
else
    step "FATAL apk build"; tail -30 $LOG; exit 1
fi

# 6. verify + stage the deliverable
APK=$ROOT/build/apk/prototype.apk
"$BUILD_TOOLS/apksigner" verify --print-certs "$APK" >> $LOG 2>&1 || { step "FATAL signature verify"; exit 1; }
"$BUILD_TOOLS/aapt" dump badging "$APK" 2>/dev/null | head -2 >> $LOG
cp -v "$APK" /home/z/my-project/download/protodroid-v0.16-latest.apk >> $LOG 2>&1
unzip -l /home/z/my-project/download/protodroid-v0.16-latest.apk | grep -E "libmain|libdxvk|protodroid-minimap" >> $LOG
# the four non-main libraries must be the exact release bytes (sha256 of the original
# ProtoDroid release APK's libraries; embedded so cleanup of old deliverables never breaks builds)
declare -A RELEASE_SHA=(
  [libSDL2.so]=30b12c49f77382b3e7588ca4407d370cd7d12749383011e575e7ae48b2e22800
  [libFAudio.so]=79609e857e3eb135330e64178c0c8005850d697b7bf354a03f395c3cf4c2db56
  [libvulkan_wrapper.so]=79057396f39144d31aed8327dc19c68d9b7d0ef5f1e31e27ea0e0fc346f4aeb8
  [libdxvk_d3d9.so]=c9b2fa054f4984ef21b6d8e618b8327113e8edfbbd42c27dfb1c8820e15dbc84
)
for lib in libSDL2.so libFAudio.so libvulkan_wrapper.so libdxvk_d3d9.so; do
    a=$(unzip -p /home/z/my-project/download/protodroid-v0.16-latest.apk lib/arm64-v8a/$lib | sha256sum | cut -d' ' -f1)
    [ "$a" = "${RELEASE_SHA[$lib]}" ] || { step "FATAL $lib differs from release bytes"; exit 1; }
done
step "release libraries byte-identical"
step "DONE: $(ls -la /home/z/my-project/download/protodroid-v0.16-latest.apk)"
touch /tmp/build_v016.done
