#!/usr/bin/env bash
# Compiles papyrus/*.psc into papyrus/compiled/*.pex with the Creation Kit's
# Papyrus compiler, run under Proton's Wine (system Wine lacks the Wine Mono the
# compiler needs). The .pex are normalized and committed, since CI can't run
# the compiler.
#
#   SKYRIM_DIR=<game folder> PROTON_DIR=<Proton Experimental>/files scripts/papyrus.sh
#
# SKYRIM_DIR holds "Papyrus Compiler/" and "Data/Source/Scripts" (the Creation
# Kit's sources). SkyUI's SKI_ConfigBase and SKI_QuestBase headers are fetched
# into build/papyrus/headers/, pinned to a commit; they state no license, so
# they are never committed.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

: "${SKYRIM_DIR:?SKYRIM_DIR not set: the Skyrim Special Edition folder with the Creation Kit installed}"
: "${PROTON_DIR:?PROTON_DIR not set: the files/ folder of Proton Experimental, e.g. <Steam>/steamapps/common/Proton - Experimental/files}"

COMPILER="$SKYRIM_DIR/Papyrus Compiler/PapyrusCompiler.exe"
SOURCES="$SKYRIM_DIR/Data/Source/Scripts"
WINE="$PROTON_DIR/bin/wine"
for path in "$COMPILER" "$SOURCES/TESV_Papyrus_Flags.flg" "$WINE"; do
	[[ -e "$path" ]] || {
		echo "not found: $path" >&2
		exit 1
	}
done

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

OUT="$ROOT/papyrus/compiled"
mkdir -p "$OUT"
export WINEPREFIX="$ROOT/build/papyrus/wineprefix" WINEDEBUG=-all
mkdir -p "$WINEPREFIX"

# Wine maps the host root to Z:. -all compiles every .psc in the folder. The
# compiler exits 0 even when a script fails, so its summary line decides.
LOG="$ROOT/build/papyrus/compile.log"
"$WINE" "$COMPILER" "Z:$ROOT/papyrus" -all -f="TESV_Papyrus_Flags.flg" \
	-i="Z:$ROOT/papyrus;Z:$HEADERS;Z:$SOURCES" -o="Z:$OUT" 2>&1 | tee "$LOG"
grep -Eq '[1-9][0-9]* succeeded, 0 failed' "$LOG" || {
	echo "Papyrus compilation failed, see $LOG" >&2
	exit 1
}

# Zero the compile times and drop the user and machine names, so a recompile
# of the same source gives the same bytes.
python3 "$ROOT/scripts/normalize_pex.py" "$OUT"/*.pex
