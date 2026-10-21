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

# Python-side tests for scripts/verify_sites.py. The tests that need a real
# SkyrimSE.exe skip themselves when it is absent, so this is safe on any host.
if command -v python3 >/dev/null 2>&1; then
	( cd "$ROOT" && python3 -m unittest discover -s tests -p 'test_*.py' )
else
	echo "python3 not found, skipping scripts/verify_sites.py tests"
fi
