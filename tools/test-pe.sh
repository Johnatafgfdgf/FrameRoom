#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIR="$(mktemp -d)"
trap 'rm -rf "$DIR"' EXIT
javac -d "$DIR" "$ROOT/app/src/main/java/dev/frameroom/PeValidator.java" "$ROOT/tests/PeValidatorTest.java"
java -cp "$DIR" PeValidatorTest
