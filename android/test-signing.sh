#!/bin/bash
# Fast apksigner coverage for the two supported keystore/password combinations. A minimal aapt2-linked fixture is
# signed and verified only in a temporary directory; it is never an APK build artifact or a native-build substitute.
set -euo pipefail

SDK=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}
if [ -z "${APKSIGNER:-}" ]; then
    for candidate in "$SDK"/build-tools/*/apksigner; do
        if [ -x "$candidate" ]; then APKSIGNER=$candidate; fi
    done
fi
if [ -z "${AAPT2:-}" ]; then
    for candidate in "$SDK"/build-tools/*/aapt2; do
        if [ -x "$candidate" ]; then AAPT2=$candidate; fi
    done
fi
if [ -z "${PLATFORM_JAR:-}" ]; then
    for candidate in "$SDK"/platforms/android-*/android.jar; do
        if [ -f "$candidate" ]; then PLATFORM_JAR=$candidate; fi
    done
fi
if [ -z "${APKSIGNER:-}" ] || [ ! -x "$APKSIGNER" ] || [ -z "${AAPT2:-}" ] || [ ! -x "$AAPT2" ] ||
   [ -z "${PLATFORM_JAR:-}" ] || [ ! -f "$PLATFORM_JAR" ]; then
    echo "ERROR: Android signing test tools not found; set APKSIGNER, AAPT2, and PLATFORM_JAR"
    exit 1
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cat > "$TMP/AndroidManifest.xml" <<'EOF'
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="com.protodroid.signingtest" android:versionCode="1" android:versionName="1">
    <uses-sdk android:minSdkVersion="26" />
    <application android:label="ProtoDroid signing test" />
</manifest>
EOF
"$AAPT2" link -o "$TMP/input.apk" -I "$PLATFORM_JAR" --manifest "$TMP/AndroidManifest.xml" \
    --auto-add-overlay --min-sdk-version 26
"$AAPT2" dump badging "$TMP/input.apk" | grep -F "package: name='com.protodroid.signingtest'"

test_case() {
    local type=$1
    local store_pass=$2
    local key_pass=$3
    local keystore="$TMP/$type.keystore"
    local signed="$TMP/signed-$type.apk"
    local report="$TMP/$type-report.txt"
    keytool -genkeypair -noprompt -keystore "$keystore" -storetype "$type" \
        -storepass "$store_pass" -alias protodroid-ci -keypass "$key_pass" \
        -keyalg RSA -keysize 2048 -validity 2 -dname "CN=ProtoDroid Signing Test,O=ProtoDroid,C=US"
    KEYSTORE_PASS="$store_pass" KEY_PASS="$key_pass" "$APKSIGNER" sign \
        --ks "$keystore" --ks-key-alias protodroid-ci --ks-pass env:KEYSTORE_PASS --key-pass env:KEY_PASS \
        --out "$signed" "$TMP/input.apk"
    "$APKSIGNER" verify --verbose --print-certs "$signed" > "$report"
    local digest
    cat "$report"
    digest=$(awk 'index($0, "certificate SHA-256 digest:") { sub(/^.*certificate SHA-256 digest:[[:space:]]*/, ""); gsub(/[[:space:]]/, ""); print; exit }' "$report" | tr 'A-F' 'a-f')
    if ! printf '%s' "$digest" | grep -Eq '^[0-9a-f]{64}$'; then
        echo "ERROR: $type signing test did not report a valid certificate digest"
        exit 1
    fi
    echo "$type signing passed (certificate ${digest:0:16}...)"
}

test_case PKCS12 shared-test-password shared-test-password
test_case JKS store-test-password different-key-password
