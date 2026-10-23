#!/usr/bin/env bash
# Native unit tests (no game, no cross-compiler).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/native"
mkdir -p "$OUT"
for t in test_curves test_hooksites test_patterns test_texts; do
	c++ -std=c++20 -Wall -Wextra -Werror -I "$ROOT/src" "$ROOT/tests/$t.cpp" -o "$OUT/$t"
	"$OUT/$t"
done

# The trampolines in src/Stubs.h can never run natively, but xbyak is
# header-only and emits x86-64 on this host too, so the generated instructions
# can be inspected. This is the only check that catches a correct patch address
# written with a wrong instruction.
# Test the find result itself: dirname of an empty string is ".", which would
# pass a -d check and compile against a directory with no xbyak in it.
XBYAK_H="$(find "$ROOT/build" -name xbyak.h 2>/dev/null | head -1)"
if [[ -n "$XBYAK_H" ]]; then
	XBYAK_INC="$(dirname "$(dirname "$XBYAK_H")")"
	c++ -std=c++20 -Wall -Wextra -Werror -I "$ROOT/src" -I "$XBYAK_INC" \
		"$ROOT/tests/test_stubs.cpp" -o "$OUT/test_stubs"
	"$OUT/test_stubs"
elif [[ -n "${BM_REQUIRE_STUB_TESTS:-}" ]]; then
	# CI sets this: there a skip would pass silently while testing nothing.
	echo "xbyak headers not found under build/, and BM_REQUIRE_STUB_TESTS is set" >&2
	exit 1
else
	echo "xbyak headers not found (run scripts/build.sh once), skipping stub tests"
fi

# Python-side tests for scripts/verify_sites.py. The tests that need a real
# SkyrimSE.exe skip themselves when it is absent, so this is safe on any host.
if command -v python3 >/dev/null 2>&1; then
	( cd "$ROOT" && python3 -m unittest discover -s tests -p 'test_*.py' )
else
	echo "python3 not found, skipping scripts/verify_sites.py tests"
fi
