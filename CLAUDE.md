# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

`BeyondMastery.dll` — an SKSE plugin for Skyrim SE 1.5.97 and AE 1.6.x (no VR, no LE) that lets the player raise each skill's cap above 100 in +10 steps, paid with perk points, dragon souls and filled black soul gems, and continues skill-dependent formulas sub-linearly or asymptotically above 100.

Two documents are the contract for this work and should be read before changing behavior:

- [PRD.md](PRD.md) — approved requirements, numbered `FR-1` … `FR-30`. Code and tests cite these IDs in comments; keep the citations accurate when you change either side.
- [docs/hook-points.md](docs/hook-points.md) — which engine function each requirement needs, with Address Library IDs, byte offsets and a confidence rating per site.

**Current milestone:** the plugin loads, logs, reads the INI, persists caps in the SKSE co-save and warns about conflicting plugins. **No hooks are installed yet and gameplay is unchanged.** The next step is verifying the hook sites in a disassembler (hook-points.md §9) before writing any hook code.

## Build and test

Development happens on a Linux host; the DLL is cross-compiled with clang-cl + lld-link against an xwin MSVC sysroot.

```bash
scripts/setup.sh    # one-time: tool check, CommonLibSSE-NG submodule, llvm-mingw, xwin sysroot, vcpkg
scripts/test.sh     # native unit tests only — fast, no cross-compiler needed
scripts/build.sh    # runs test.sh, then configures + builds BeyondMastery.dll
```

`setup.sh` prints three environment variables (`VCPKG_ROOT`, `XWIN_SYSROOT`, `LLVM_MINGW_BIN`) that must be exported before `build.sh` works. The first `build.sh` compiles CommonLibSSE-NG and all vcpkg ports and takes a long time; later builds are incremental via Ninja in `build/release/`.

There is only one CMake preset, `linux-clangcl-release`. Direct equivalent of `build.sh`:

```bash
cmake --preset linux-clangcl-release && cmake --build --preset release
```

### Unit tests

Each file in `tests/` is a self-contained binary with `CHECK`/`CHECK_NEAR` macros and a hand-written `main()` — no test framework. `scripts/test.sh` loops over them and compiles each with `c++ -std=c++20 -Wall -Wextra -Werror -I src`, entirely outside CMake; add a new test by adding its name to that loop. Currently `test_curves` (the math in `src/Curves.h`) and `test_hooksites` (every pattern in `src/HookSites.h` is well-formed).

To run one group of tests, call just that function from `main()`, or build and run directly:

```bash
c++ -std=c++20 -I src tests/test_curves.cpp -o build/native/test_curves && build/native/test_curves
```

This is the only automated test layer, and it can only cover the game-free headers (`src/Curves.h`, `src/HookSites.h`). Everything else needs the game. A cross-compiled DLL can also crash in-game on ABI/layout mismatches the compiler cannot see, so in-game testing is mandatory before claiming a change works.

## Architecture

### Game-free math core

`src/Curves.h` holds every formula — step costs, XP gain multiplier, form A (`SqrtEffectiveSkill`), form B (`Asymptote`), `PowerDecayFormula` for spell/enchanting cost, `BarterFormula` — as header-only pure functions in `namespace BM::Curves`.

It must stay free of `RE/`, `SKSE/` and `PCH.h` includes and of C++23-only features, because `scripts/test.sh` compiles it standalone as C++20 with `-Werror`. The plugin itself builds as C++23 with `src/PCH.h` force-included. **New math belongs here, with a test, not inline in a hook.**

`PowerDecayFormula` deliberately provides both `Factor()` (compute the value ourselves) and `EffectiveSkill()` (invert the vanilla formula so the engine computes the right value from a faked skill). Which one a hook uses depends on whether we replace the engine's calculation or feed it a remapped skill — see below.

### Two scaling mechanisms (PRD §6.9, hook-points.md §5)

The core design decision: instead of one hook per skill-dependent formula, there is **one hook on the player's `GetActorValue`** that returns a remapped effective skill `s_eff` for skill actor values above 100. `src/Skills.h` maps each of the 18 skills to its `Scaling` form (`kSqrt`, `kSpellCost`, `kBarter`); `ScalingOf()` is the single place that decides.

Consequences to keep in mind:

- Every other reader of the player's skill — Papyrus, perk conditions, other mods — also sees `s_eff`. Values stay ≥ 100, so vanilla perk requirements still pass.
- The skills menu must have the remap disabled while it builds its list (FR-29), which is why there is a second hook on the menu update function.
- Magic skills and Enchanting charge cost are vanilla-clamped at 100 inside the engine's formulas; those clamps must be patched out, or form B has no effect.
- Enchanting is the exception to the one-hook rule: its charge-per-use call is replaced and computed from the **real** skill (FR-30), while its magnitude goes through the normal `s_eff` path.
- Barter (FR-26, hook-points.md §6) is still unresolved — confidence L, no reference implementation. It needs reverse engineering before it can be implemented.

### Plugin units

`src/main.cpp` is the `SKSEPluginLoad` entry point: init, config load, serialization registration, and a messaging listener that detects conflicting SKSE plugins at `kPostLoad` and notifies once at `kDataLoaded` (FR-20).

`src/Config.*` is a singleton read from `Data/SKSE/Plugins/BeyondMastery.ini` via SimpleIni. `Load()` validates and clamps every value, then **writes the whole file back** so new keys appear for users after an update — add new settings in both the read and the write-back block, with the comment string.

`src/State.*` is a singleton holding the 18 caps, persisted in the SKSE co-save under unique ID `'BMST'`, record `'CAPS'` version 1. The loader is defensive by contract (FR-22): unknown record, wrong version/length or an implausible cap value falls back to 100 rather than failing. Bumping the record layout means bumping `kCapsVersion` and handling the old version in `OnLoad`.

## Working on hooks

- Resolve addresses through Address Library IDs only — never hardcoded offsets (PRD §7). SE and AE IDs differ and often need separate wrappers because the surrounding instructions differ.
- Check the byte signature at every patch site before writing, and log-and-skip on mismatch rather than patching blind. A failed signature check is also the second signal for conflict detection (hook-points.md §8).
- Prefer not installing a hook that the current config makes a no-op (e.g. the XP-gain hook at default `f = 1.0`, the character-XP hook when `bClampCharXPAbove100` is false) — fewer hooks, fewer conflicts.
- Take IDs and offsets from hook-points.md as *facts about the binary to re-verify*, not as working code: the Rust uncapper referenced there has no license file and nothing may be copied from it.
- Derive formula constants from the disassembly or from GMSTs read at runtime, not from UESP values.

## License constraint

CommonLibSSE-NG is GPL-3.0-or-later and statically linked, so this project is GPL-3.0-or-later and its source must be distributable. Do not add dependencies with incompatible licenses.
