#!/bin/bash
set -u
DATA=$HOME/protodroid-data
LOG=/tmp/glslang_build.log
: > $LOG
cd $DATA/deps-src
export PATH=/home/z/.venv/bin:$PATH
if [ ! -x $DATA/glslang/bin/glslangValidator ]; then
    rm -rf glslang-build
    cmake -S glslang -B glslang-build -GNinja -DCMAKE_BUILD_TYPE=Release \
        -DENABLE_OPT=OFF -DGLSLANG_TESTS=OFF -DGLSLANG_ENABLE_INSTALL=OFF >> $LOG 2>&1 || { echo FATAL_CMAKE >> $LOG; exit 1; }
    ninja -C glslang-build StandAlone/glslang >> $LOG 2>&1 || { echo FATAL_NINJA >> $LOG; exit 1; }
    mkdir -p $DATA/glslang/bin
    cp glslang-build/StandAlone/glslang $DATA/glslang/bin/glslangValidator || { echo FATAL_CP >> $LOG; exit 1; }
    cp glslang-build/StandAlone/glslang $DATA/glslang/bin/glslang
    chmod +x $DATA/glslang/bin/*
    rm -rf glslang-build  # 4.2G disk: the binary ships, the build tree goes
fi
$DATA/glslang/bin/glslangValidator --version >> $LOG 2>&1
echo GLSLANG_DONE >> $LOG
touch /tmp/glslang.done
