# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

`BeyondMastery.dll` — an SKSE plugin for Skyrim AE 1.6.x and 1.7.x (no SE 1.5.97 for now — PRD §3; no VR, no LE) that lets the player raise each skill's cap above 100 in +10 steps, paid with perk points, dragon souls and filled black soul gems, and continues skill-dependent formulas sub-linearly or asymptotically above 100.

Two documents are the contract for this work and should be read before changing behavior:

- [PRD.md](PRD.md) — approved requirements, numbered `FR-1` … `FR-30`. Code and tests cite these IDs in comments; keep the citations accurate when you change either side.
- [docs/hook-points.md](docs/hook-points.md) — which engine function each requirement needs, with Address Library IDs, byte offsets and a confidence rating per site.

**Current milestone:** all nine hook sites are verified against a real `SkyrimSE.exe` — run `scripts/verify_sites.py` to repeat it (hook-points.md §9a). Cap enforcement (FR-2, FR-8) and the XP gain factor (FR-9) are implemented and installed on AE/1.7.x; the XP hook is skipped at the default `fXPGainFactorPerTier` of 1.0, where it would be a no-op.

**Verified in-game on 1.7.104** (SKSE64 2.3.1, Proton): the plugin loads, all nine sites resolve live to the addresses `verify_sites.py` predicts, and the cap hook installs and runs on every skill XP gain without crashing. Skills level normally and stop at the default cap of 100 (One-Handed via `player.advskill`, Alchemy). With every cap at 100 that is indistinguishable from vanilla, so a *raised* cap has not been exercised — that needs the cap-raise transaction.

Out of scope for now: SE 1.5.97 (PRD §3). Its patch shapes are unconfirmed without a 1.5.97 binary, so on SE the plugin loads and leaves everything vanilla; the SE IDs in `HookSites.h` stay as a starting point. Deliberately not done: FR-10's double XP accumulator, FR-11's character-XP clamp, the cap-raise transaction and menu, and all formula scaling. See `docs/superpowers/plans/` and that plan's ledger for the reasoning.

## Build and test

Development happens on a Linux host; the DLL is cross-compiled with clang-cl + lld-link against an xwin MSVC sysroot.

```bash
scripts/setup.sh    # one-time: tool check, CommonLibSSE-NG submodule, llvm-mingw, xwin sysroot, vcpkg
scripts/test.sh     # native unit tests only — fast, no cross-compiler needed
scripts/build.sh    # runs test.sh, then configures + builds BeyondMastery.dll
```

`setup.sh` prints three environment variables (`VCPKG_ROOT`, `XWIN_SYSROOT`, `LLVM_MINGW_BIN`) that must be exported before `build.sh` works. The first `build.sh` compiles CommonLibSSE-NG and all vcpkg ports and takes a long time; later builds are incremental via Ninja in `build/release/`.

The plugin links the C++ runtime **statically** (`/MT`, overlay triplet `cmake/triplets/x64-windows-clangcl-static.cmake`). Do not switch back to `/MD`: the player's `msvcp140.dll` can be arbitrarily old — the first in-game run met the 2016 VS 2015 U3 runtime in the Proton prefix — and STL 14.40+ `std::mutex` faults on its first lock against it. `scripts/check_runtime_deps.py` fails the build if `msvcp140`/`vcruntime140` ever reappear in the import table.

There is only one CMake preset, `linux-clangcl-release`. Direct equivalent of `build.sh`:

```bash
cmake --preset linux-clangcl-release && cmake --build --preset release
```

### Unit tests

The C++ tests in `tests/` are self-contained binaries with `CHECK`/`CHECK_EQ`/`CHECK_NEAR` macros and a hand-written `main()` — no test framework. `scripts/test.sh` loops over them and compiles each with `c++ -std=c++20 -Wall -Wextra -Werror -I src`, entirely outside CMake; add a new one by adding its name to that loop. The Python tests run under stdlib `unittest` (this host has no pytest).

| Test | Covers |
|---|---|
| `test_curves` | the math in `src/Curves.h` |
| `test_hooksites` | the site table and the runtime gate in `src/HookSites.h` |
| `test_patterns` | byte-signature matching in `src/Patterns.h` |
| `test_stubs` | the hand-written trampolines in `src/Stubs.h`, **emitted with xbyak and inspected** — frame arithmetic, registers, budget. Needs the xbyak headers from the vcpkg install, which is why `build.sh` configures before testing. |
| `test_verify_sites.py` | Address Library parsing and site verification against the real `SkyrimSE.exe` (skips when absent) |
| `test_runtime_deps.py` | the build gate that keeps the plugin off the player's C++ runtime |

To run one group of tests, call just that function from `main()`, or build and run directly:

```bash
c++ -std=c++20 -I src tests/test_curves.cpp -o build/native/test_curves && build/native/test_curves
```

This is the only automated test layer, and it can only cover the game-free headers (`Curves.h`, `HookSites.h`, `Patterns.h`, `Stubs.h`). Everything else needs the game. A cross-compiled DLL can also crash in-game on ABI/layout mismatches the compiler cannot see, so in-game testing is mandatory before claiming a change works.

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
- Reach any member of a game structure whose layout differs between runtimes through CommonLibSSE-NG's runtime accessors — `RUNTIME_*_ACCESSOR*` (e.g. `AsActorValueOwner()`), `GetXRuntimeData()`, `REL::RelocateMember*` — never through a hardcoded offset or a direct member. The plugin declares `StructCompatibility::Independent` (SKSE's `NoStructUse`), and that claim is only true because one binary resolves every such offset from the running game's version. `PlayerCharacter`'s perk count and skills data are 1.6.629-sensitive and are next in line for this (the cap-raise transaction).

## License constraint

CommonLibSSE-NG is GPL-3.0-or-later and statically linked, so this project is GPL-3.0-or-later and its source must be distributable. Do not add dependencies with incompatible licenses.
