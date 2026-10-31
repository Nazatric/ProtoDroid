#!/bin/bash
# Runs the real driver package installer on a host JDK. JSON-java is test-only; Android supplies org.json in the APK.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
JSON_REV=f9b5587c87aaf02e4a5ed1991d48d6c05993624a
WORK=$(mktemp -d /tmp/protodroid-driver-test.XXXXXX)
trap 'rm -rf "$WORK"' EXIT
JSON_SRC=${JSON_SRC:-$WORK/json-java}
if [ ! -d "$JSON_SRC/src/main/java/org/json" ]; then
    git init -q "$JSON_SRC"
    git -C "$JSON_SRC" remote add origin https://github.com/stleary/JSON-java.git
    git -C "$JSON_SRC" fetch -q --depth 1 origin "$JSON_REV"
    git -C "$JSON_SRC" checkout -q FETCH_HEAD
fi
find "$JSON_SRC/src/main/java/org/json" -name '*.java' -print > "$WORK/sources"
printf '%s\n' "$ROOT/android/java/com/protodroid/DriverPackage.java" \
    "$ROOT/android/tests/com/protodroid/DriverPackageTest.java" >> "$WORK/sources"
javac --release 11 -d "$WORK/classes" @"$WORK/sources"
java -cp "$WORK/classes" com.protodroid.DriverPackageTest
