#!/usr/bin/env bash
# Formats the project's C++ sources with clang-format (.clang-format).
#
#   scripts/format.sh           fix in place
#   scripts/format.sh --check   report violations and exit non-zero; changes nothing
#
# Uses llvm-mingw's clang-format, which setup.sh installs at a pinned version:
# formatting output differs between clang-format versions, so every machine
# should run the same one. Override with CLANG_FORMAT=/path/to/clang-format.
# Only src/ and tests/ are formatted; lib/ is a submodule with its own style.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

CF="${CLANG_FORMAT:-}"
if [[ -z "$CF" ]]; then
	for candidate in "${LLVM_MINGW_BIN:-$HOME/.local/opt/llvm-mingw/bin}/clang-format" "$(command -v clang-format || true)"; do
		if [[ -n "$candidate" && -x "$candidate" ]]; then
			CF="$candidate"
			break
		fi
	done
fi
if [[ -z "$CF" || ! -x "$CF" ]]; then
	echo "clang-format not found; run scripts/setup.sh or set CLANG_FORMAT" >&2
	exit 2
fi

# Relative paths, so editors can turn reported violations into links.
mapfile -t FILES < <(find src tests -type f \( -name '*.h' -o -name '*.cpp' \) | sort)

case "${1:-}" in
--check)
	if "$CF" --dry-run --Werror "${FILES[@]}"; then
		echo "format: ${#FILES[@]} files clean ($("$CF" --version | sed 's/ (.*//'))"
	else
		echo "format: violations found; run scripts/format.sh to fix" >&2
		exit 1
	fi
	;;
"")
	"$CF" -i "${FILES[@]}"
	echo "format: ${#FILES[@]} files formatted ($("$CF" --version | sed 's/ (.*//'))"
	;;
*)
	echo "usage: scripts/format.sh [--check]" >&2
	exit 2
	;;
esac
