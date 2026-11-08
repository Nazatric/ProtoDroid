#!/bin/bash
# ProtoDroid 0.16 build environment: Stage B
# Build Android dependencies: Dobby, SDL2, FAudio, FFmpeg, DXVK (d3d9), mesa Vulkan wrapper.
set -uo pipefail
DATA=$HOME/protodroid-data
LOG=$DATA/stageB.log
: > $LOG
step() { echo "[$(date +%H:%M:%S)] $1" >> $LOG; }

export JAVA_HOME=$DATA/jdk
export PATH=$DATA/jdk/bin:$DATA/glslang/usr/bin:$HOME/.venv/bin:$PATH
export ANDROID_NDK_HOME=$DATA/ndk
export ANDROID_HOME=$DATA/android-sdk
export JOBS=2
ROOT=/home/z/my-project/work/protodroid-0.17.4/ProtoDroid-protodroid-v0.15

step "building deps for arm64-v8a (this includes DXVK - the long pole)"
if bash $ROOT/android/build-deps.sh arm64-v8a $DATA/deps-src $DATA/android-deps/arm64-v8a >> $LOG 2>&1; then
    step "build-deps OK"
else
    step "FATAL: build-deps failed"
    tail -30 $LOG
    exit 1
fi

step "building vulkan wrapper"
if bash $ROOT/android/build-vulkan-wrapper.sh arm64-v8a $DATA/deps-src $DATA/android-deps/arm64-v8a >> $LOG 2>&1; then
    step "vulkan wrapper OK"
else
    step "FATAL: vulkan wrapper failed"
    tail -30 $LOG
    exit 1
fi

ls -la $DATA/android-deps/arm64-v8a/lib/ >> $LOG
step "STAGE B COMPLETE"
touch $DATA/stageB.done
