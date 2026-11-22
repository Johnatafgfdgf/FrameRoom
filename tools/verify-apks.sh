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
"$ANDROID_HOME/build-tools/35.0.0/zipalign" -c -P 16 4 "$apk"
python3 - "$apk" "$arena" <<'PYELF'
from pathlib import Path
from struct import unpack_from
from sys import argv
from zipfile import ZipFile

with ZipFile(argv[1]) as apk:
    assert apk.read("assets/arena.apk") == Path(argv[2]).read_bytes(), "Wrong embedded Arena APK"
    for name in apk.namelist():
        if not name.endswith(".so"):
            continue
        data = apk.read(name)
        assert data[:6] == b"\x7fELF\x02\x01", name
        assert unpack_from("<H", data, 18)[0] == 183, name
        table = unpack_from("<Q", data, 32)[0]
        entry_size, entries = unpack_from("<HH", data, 54)
        loads = 0
        for index in range(entries):
            kind, _, offset, address, _, _, _, alignment = unpack_from("<IIQQQQQQ", data, table + index * entry_size)
            if kind != 1:
                continue
            loads += 1
            assert alignment >= 16384 and (offset - address) % 16384 == 0, name
        assert loads, name
PYELF
echo "PASS: the embedded Arena is the separately built APK; both signatures, ARM64, 16 KiB alignment and DLL exclusion verified."
sha256sum "$apk" "$arena"
