#!/usr/bin/env bash
# Compiles papyrus/*.psc into papyrus/compiled/*.pex with the toolchain's
# papyrus.sh, which runs the Creation Kit's compiler and normalizes the .pex.
# They are committed, since CI can't run the compiler.
#
#   SKYRIM_DIR=<game folder> PROTON_DIR=<Proton Experimental>/files scripts/papyrus.sh
#
# What the two variables name is in the toolchain script's header. This
# script only adds SkyUI's SKI_ConfigBase and SKI_QuestBase headers, fetched
# into build/papyrus/headers/ at a pinned commit; they state no license, so
# they are never committed.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

SKYUI_SHA=835428728e2305865e220fdfc99d791434955eb1
HEADERS="$ROOT/build/papyrus/headers"
mkdir -p "$HEADERS"
for file in SKI_ConfigBase.psc SKI_QuestBase.psc; do
	# Download beside it, then rename: an interrupted curl leaves no partial header.
	if [[ ! -s "$HEADERS/$file" ]]; then
		curl -fsSL -o "$HEADERS/$file.part" \
			"https://raw.githubusercontent.com/schlangster/skyui/$SKYUI_SHA/dist/Data/Scripts/Headers/$file"
		mv "$HEADERS/$file.part" "$HEADERS/$file"
	fi
done

exec "$ROOT/lib/skyrim-mod-toolchain/scripts/papyrus.sh" "$ROOT/papyrus" "$ROOT/papyrus/compiled" "$HEADERS"
