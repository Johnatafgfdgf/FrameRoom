#!/usr/bin/env bash
set -euo pipefail
apk="${1:-app/build/outputs/apk/debug/app-debug.apk}"
arena="${2:-samplegame/build/outputs/apk/debug/samplegame-debug.apk}"
signer="${ANDROID_HOME:?Set ANDROID_HOME}/build-tools/35.0.0/apksigner"
main_signature="$("$signer" verify --print-certs "$apk")"
arena_signature="$("$signer" verify --print-certs "$arena")"
digest() { awk '/^Signer #1 certificate SHA-256 digest:/ { print $NF }' <<<"$1"; }
main_digest="$(digest "$main_signature")"
test -n "$main_digest"
test "$main_digest" = "$(digest "$arena_signature")"
apk_files="$(unzip -Z1 "$apk")"
for library in liblsfgprobe libbloodframe libc++_shared; do
    grep -Fxq "lib/arm64-v8a/$library.so" <<<"$apk_files"
done
if grep -Ei '\.dll$' <<<"$apk_files" || grep '^lib/' <<<"$apk_files" | grep -v '^lib/arm64-v8a/'; then
    echo "APK contains an unexpected library or DLL."
    exit 1
fi
echo "PASS: both APK signatures match; FrameRoom contains only ARM64 native libraries and no DLL."
sha256sum "$apk" "$arena"
