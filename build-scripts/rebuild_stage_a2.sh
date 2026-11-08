#!/bin/bash
# Stage A2: the pieces the old build-kit's setup_env.sh provided — JDK 17, SDK packages, glslang.
# Then the stage B path fix (the 0.15 tree lives inside protodroid-0.17.4).
set -u
DATA=$HOME/protodroid-data
DL=$DATA/dl
SDK=$DATA/android-sdk
LOG=/tmp/rebuild_a2.log
: > $LOG
step() { echo "[$(date +%H:%M:%S)] $1" >> $LOG; }
mkdir -p $DL

# 1. JDK 17 (temurin, the plain tarball)
if [ ! -x $DATA/jdk/bin/java ]; then
    step "downloading JDK 17"
    curl -sL -o $DL/jdk.tar.gz "https://api.adoptium.net/v3/binary/latest/17/ga/linux/x64/jdk/hotspot/normal/eclipse" || { step "FATAL jdk dl"; exit 1; }
    rm -rf $DATA/jdk && mkdir -p $DATA/jdk
    tar xzf $DL/jdk.tar.gz -C $DATA/jdk --strip-components=1 && rm -f $DL/jdk.tar.gz
    step "jdk ready: $($DATA/jdk/bin/java -version 2>&1 | head -1)"
fi

# 2. SDK packages
export ANDROID_HOME=$SDK
SDKM=$SDK/cmdline-tools/latest/bin/sdkmanager
if [ ! -f $SDK/platforms/android-35/android.jar ] || [ ! -d $SDK/build-tools/36.0.0 ]; then
    step "installing SDK packages"
    yes | $SDKM --licenses >> $LOG 2>&1
    yes | $SDKM "platforms;android-35" "build-tools;36.0.0" "platform-tools" >> $LOG 2>&1 || { step "FATAL sdkmanager"; tail -5 $LOG; exit 1; }
    step "sdk packages ready"
fi

# 3. glslang (dxvk build needs the host glslangValidator)
if [ ! -x $DATA/glslang/bin/glslangValidator ] && [ ! -x $DATA/glslang/usr/bin/glslangValidator ]; then
    step "building glslang"
    mkdir -p $DATA/deps-src
    if [ ! -d $DATA/deps-src/glslang ]; then
        git clone --depth 1 https://github.com/KhronosGroup/glslang.git $DATA/deps-src/glslang >> $LOG 2>&1 || { step "FATAL glslang clone"; tail -5 $LOG; exit 1; }
    fi
    mkdir -p $DATA/glslang/usr/bin
    bash /home/z/my-project/scripts/build_glslang.sh >> $LOG 2>&1 || { step "FATAL glslang"; tail -10 /tmp/glslang_build.log 2>/dev/null; tail -10 $LOG; exit 1; }
    mkdir -p $DATA/glslang/usr/bin && cp -f $DATA/glslang/bin/* $DATA/glslang/usr/bin/
    step "glslang ready"
fi

# 4. fix stage B ROOT path (the 0.15 tree moved inside protodroid-0.17.4) and run it
sed -i 's|^ROOT=/home/z/my-project/work/protodroid-0.15/ProtoDroid-protodroid-v0.15$|ROOT=/home/z/my-project/work/protodroid-0.17.4/ProtoDroid-protodroid-v0.15|' /home/z/my-project/scripts/build_deps_stageB.sh
step "stage B start"
bash /home/z/my-project/scripts/build_deps_stageB.sh >> $LOG 2>&1 || { step "FATAL stage B"; tail -12 $DATA/stageB.log 2>/dev/null; tail -12 $LOG; exit 1; }
step "A2 + B COMPLETE"
touch /tmp/stage_a2.done
