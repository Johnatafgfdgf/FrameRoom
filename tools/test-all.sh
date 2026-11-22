#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
bash "$ROOT/tools/test-pe.sh"
python3 "$ROOT/tools/check-source.py"
python3 "$ROOT/tools/patch_bloodframe_ahb.py" --self-test
