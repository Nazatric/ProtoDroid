#!/bin/bash
# Fast apksigner coverage for the two supported keystore/password combinations. This signs small ZIP inputs only;
# it never produces or substitutes an APK build artifact.
set -euo pipefail

if [ -z "${APKSIGNER:-}" ]; then
    SDK=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}
    for candidate in "$SDK"/build-tools/*/apksigner; do
        if [ -x "$candidate" ]; then APKSIGNER=$candidate; fi
    done
fi
if [ -z "${APKSIGNER:-}" ] || [ ! -x "$APKSIGNER" ]; then
    echo "ERROR: apksigner not found; set APKSIGNER or ANDROID_SDK_ROOT"
    exit 1
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
printf 'ProtoDroid signing test\n' > "$TMP/input.txt"
(cd "$TMP" && zip -q input.zip input.txt)

test_case() {
    local type=$1
    local store_pass=$2
    local key_pass=$3
    local keystore="$TMP/$type.keystore"
    local signed="$TMP/signed-$type.zip"
    local report="$TMP/$type-report.txt"
    keytool -genkeypair -noprompt -keystore "$keystore" -storetype "$type" \
        -storepass "$store_pass" -alias protodroid-ci -keypass "$key_pass" \
        -keyalg RSA -keysize 2048 -validity 2 -dname "CN=ProtoDroid Signing Test,O=ProtoDroid,C=US"
    KEYSTORE_PASS="$store_pass" KEY_PASS="$key_pass" "$APKSIGNER" sign \
        --ks "$keystore" --ks-key-alias protodroid-ci --ks-pass env:KEYSTORE_PASS --key-pass env:KEY_PASS \
        --out "$signed" "$TMP/input.zip"
    "$APKSIGNER" verify --verbose --print-certs "$signed" > "$report"
    local digest
    digest=$(awk -F': ' '/Signer #1 certificate SHA-256 digest:/ {print $2; exit}' "$report")
    if ! printf '%s' "$digest" | grep -Eq '^[0-9a-f]{64}$'; then
        echo "ERROR: $type signing test did not report a valid certificate digest"
        exit 1
    fi
    echo "$type signing passed (certificate ${digest:0:16}...)"
}

test_case PKCS12 shared-test-password shared-test-password
test_case JKS store-test-password different-key-password
