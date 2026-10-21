#!/usr/bin/env bash
# Native unit tests (no game, no cross-compiler).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/native"
mkdir -p "$OUT"
for t in test_curves test_hooksites; do
	c++ -std=c++20 -Wall -Wextra -Werror -I "$ROOT/src" "$ROOT/tests/$t.cpp" -o "$OUT/$t"
	"$OUT/$t"
done
