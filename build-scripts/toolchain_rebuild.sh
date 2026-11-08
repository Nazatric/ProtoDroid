#!/bin/bash
# Full toolchain rebuild after the sandbox recycle: cmdline-tools, NDK r26d, JDK, glslang,
# SDK packages, source clones (stage A), then Dobby/SDL2/FAudio/FFmpeg/DXVK/mesa (stage B).
set -u
DATA=$HOME/protodroid-data
DL=$DATA/dl
SDK=$DATA/android-sdk
LOG=/tmp/toolchain_rebuild.log
: > $LOG
step() { echo "[$(date +%H:%M:%S)] $1" >> $LOG; }
mkdir -p $DATA/dl $SDK

# 1. cmdline-tools (sdkmanager needs it)
if [ ! -x $SDK/cmdline-tools/latest/bin/sdkmanager ]; then
    step "downloading cmdline-tools"
    curl -sL -o $DL/clt.zip https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip || { step "FATAL clt"; exit 1; }
    mkdir -p $SDK/cmdline-tools && unzip -qo $DL/clt.zip -d $SDK/cmdline-tools && rm -f $DL/clt.zip
    mv $SDK/cmdline-tools/cmdline-tools $SDK/cmdline-tools/latest
    step "cmdline-tools ready"
fi
export ANDROID_HOME=$SDK
SDKM=$SDK/cmdline-tools/latest/bin/sdkmanager

# 2. NDK r28c (clang 19.0.1 — matches the original build banner) as a direct zip,
#    extracted where setup_env.sh expects it
NDK_DIR=$DATA/ndk-r28c
if [ ! -d $NDK_DIR/toolchains/llvm/prebuilt/linux-x86_64/bin ]; then
    step "downloading NDK r28c"
    curl -sL -o $DL/ndk.zip https://dl.google.com/android/repository/android-ndk-r28c-linux.zip || { step "FATAL ndk dl"; exit 1; }
    rm -rf $NDK_DIR && mkdir -p $NDK_DIR/.x && unzip -qo $DL/ndk.zip -d $NDK_DIR/.x && rm -f $DL/ndk.zip
    mv $NDK_DIR/.x/android-ndk-r28c/* $NDK_DIR/ && rm -rf $NDK_DIR/.x
fi
ln -sfn $NDK_DIR $DATA/ndk
step "NDK ready: $($NDK_DIR/toolchains/llvm/prebuilt/linux-x86_64/bin/clang -v 2>&1 | head -1)"

# 3. Python build tools into the surviving venv
/home/z/.venv/bin/pip3 install -q cmake ninja meson mako ply pyyaml packaging >> $LOG 2>&1
step "python tools ok"

# 4. Stage A: JDK, glslang, platform/build-tools, source clones
if bash /home/z/my-project/download/build-kit/setup_env.sh >> $LOG 2>&1; then
    step "stage A OK"
else
    step "FATAL stage A"; tail -20 $LOG; exit 1
fi

# 5. Stage B: build all Android deps (DXVK is the long pole)
ROOT=/home/z/my-project/work/protodroid-0.17.4/ProtoDroid-protodroid-v0.15
if bash /home/z/my-project/download/build-kit/build_deps_stageB.sh >> $LOG 2>&1; then
    step "stage B OK"
else
    step "FATAL stage B"; tail -30 $LOG; exit 1
fi
ls -la $DATA/android-deps/arm64-v8a/lib/ >> $LOG 2>&1
step "TOOLCHAIN REBUILD COMPLETE"
touch /tmp/toolchain.done
