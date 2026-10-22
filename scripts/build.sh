#!/usr/bin/env bash
# Runs the native unit tests, then cross-compiles BeyondMastery.dll.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

: "${VCPKG_ROOT:?VCPKG_ROOT not set, run scripts/setup.sh first}"
export LLVM_MINGW_BIN="${LLVM_MINGW_BIN:-$HOME/.local/opt/llvm-mingw/bin}"

# Configure first: it runs the vcpkg install, which provides the xbyak headers
# the stub tests compile against. Testing before configuring would silently
# skip those tests on every clean build.
cmake --preset linux-clangcl-release
"$ROOT/scripts/test.sh"
cmake --build --preset release

DLL="build/release/BeyondMastery.dll"
file "$DLL"
echo "Built $DLL"
echo "Install: copy it to <Skyrim>/Data/SKSE/Plugins/ (or package it for your mod manager)."
