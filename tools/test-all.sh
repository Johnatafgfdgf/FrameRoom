#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
bash "$ROOT/tools/test-pe.sh"
python3 "$ROOT/tools/check-source.py"
python3 "$ROOT/tools/patch_bloodframe_ahb.py" --self-test
scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I "$ROOT/app/src/main/cpp" "$ROOT/tests/frame_validation_test.cpp" -o "$scratch/frame-validation"
"$scratch/frame-validation"
