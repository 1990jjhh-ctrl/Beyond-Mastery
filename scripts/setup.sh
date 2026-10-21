#!/usr/bin/env bash
# One-time setup for cross-compiling Beyond Mastery on a Linux host.
# Follows CommonLibSSE-NG's examples/linux-cross-compile/README.md.
#
# System packages need sudo and are only checked, not installed.
# Everything else goes to your home directory.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
XWIN_OUT="${XWIN_SYSROOT:-$HOME/xwin-out}"
XWIN_CACHE="$HOME/xwin-cache"
VCPKG_DIR="${VCPKG_ROOT:-$HOME/.local/share/vcpkg}"

need() { command -v "$1" >/dev/null 2>&1; }

echo "== 1. System tools"
missing=()
for t in git curl cmake ninja wine; do need "$t" || missing+=("$t"); done
need clang-cl || ls /usr/bin/clang-cl-* >/dev/null 2>&1 || missing+=("clang (clang-cl)")
need lld-link || ls /usr/bin/lld-link-* >/dev/null 2>&1 || missing+=("lld (lld-link)")
need llvm-rc  || ls /usr/bin/llvm-rc-*  >/dev/null 2>&1 || missing+=("llvm (llvm-rc)")
need cargo || missing+=("rust/cargo (rustup)")
if ((${#missing[@]})); then
	echo "Missing: ${missing[*]}"
	echo "Debian/Ubuntu: sudo apt-get install clang lld llvm cmake ninja-build wine git"
	echo "Arch:          sudo pacman -S clang lld llvm cmake ninja wine git"
	echo "Then re-run this script."
	exit 1
fi

echo "== 1b. llvm-mingw (builds the fxc2 shader stand-in for directxtk, no sudo)"
LLVM_MINGW_VER="20260826"  # pinned as in CommonLibSSE-NG's linux-cross-compile README
LLVM_MINGW_DIR="$HOME/.local/opt/llvm-mingw"
if [[ -n "${LLVM_MINGW_BIN:-}" && -x "$LLVM_MINGW_BIN/x86_64-w64-mingw32-clang++" ]] || need x86_64-w64-mingw32-clang++; then
	echo "found"
elif [[ -x "$LLVM_MINGW_DIR/bin/x86_64-w64-mingw32-clang++" ]]; then
	echo "found in $LLVM_MINGW_DIR"
else
	tarball="llvm-mingw-${LLVM_MINGW_VER}-ucrt-ubuntu-22.04-x86_64.tar.xz"
	tmp="$(mktemp -d)"
	echo "Downloading $tarball"
	curl -fL --retry 3 -o "$tmp/$tarball" \
		"https://github.com/mstorsjo/llvm-mingw/releases/download/${LLVM_MINGW_VER}/${tarball}"
	mkdir -p "$LLVM_MINGW_DIR"
	tar -xf "$tmp/$tarball" -C "$LLVM_MINGW_DIR" --strip-components=1
	rm -rf "$tmp"
	"$LLVM_MINGW_DIR/bin/x86_64-w64-mingw32-clang++" --version | head -1
fi

echo "== 2. Git repository and CommonLibSSE-NG submodule"
cd "$ROOT"
[[ -d .git ]] || git init -q
if [[ ! -f lib/commonlibsse-ng/CMakeLists.txt ]]; then
	git submodule add -b ng https://github.com/alandtse/CommonLibSSE-NG.git lib/commonlibsse-ng
fi
git submodule update --init --recursive

echo "== 3. xwin (Windows SDK + MSVC CRT sysroot)"
need xwin || cargo install xwin --version 0.10.0 --locked
if [[ ! -d "$XWIN_OUT" ]]; then
	mkdir -p "$XWIN_CACHE" "$XWIN_OUT"
	echo "Downloading the MSVC CRT and Windows SDK. You accept Microsoft's license by continuing."
	xwin --accept-license --http-retry 10 --cache-dir "$XWIN_CACHE" splat \
		--output "$XWIN_OUT" --include-debug-libs \
		--use-winsysroot-style --preserve-ms-arch-notation
fi

echo "== 4. vcpkg"
if [[ ! -x "$VCPKG_DIR/vcpkg" ]]; then
	git clone https://github.com/microsoft/vcpkg "$VCPKG_DIR"
	"$VCPKG_DIR/bootstrap-vcpkg.sh" -disableMetrics
fi

cat <<EOF

Setup done. Add to your shell profile:
  export VCPKG_ROOT="$VCPKG_DIR"
  export XWIN_SYSROOT="$XWIN_OUT"
  export LLVM_MINGW_BIN="\${LLVM_MINGW_BIN:-$LLVM_MINGW_DIR/bin}"
  export PATH="\$HOME/.cargo/bin:\$PATH"

Then build with: scripts/build.sh
EOF
