# Development

How Beyond Mastery is built, tested and put together. For what the mod does and its current status, see [README.md](../README.md).

## Contract documents

Read these before changing behavior:

- [PRD.md](../PRD.md) — the approved requirements, numbered `FR-n`. Code and tests cite these IDs in comments, so keep the citations accurate when you change either side.
- [hook-points.md](hook-points.md) — which engine function each requirement needs, with Address Library IDs, byte offsets and a confidence rating per site. §9a has the verification table.
- [superpowers/plans/](superpowers/plans/) — the implementation plans, with the reasoning behind what is done and what is deliberately left out.

## Contributing

Every change goes through a branch and a pull request; nothing is committed to `master` directly.

1. **Branch** from `master`, named after the change's type: `feat/…`, `fix/…`, `docs/…`, `ci/…`.
2. **Commit** with [Conventional Commits](https://www.conventionalcommits.org) titles: `<type>(<scope>)?: <description>`, at most 72 characters, body lines at most 80. Types: `feat`, `fix`, `docs`, `build`, `ci`, `chore`, `refactor`, `style`, `test`, `perf`, `revert`. The rules live in `.gitlint`. Run `gitlint install-hook` once per clone, so a bad message is rejected when you commit rather than in CI.
3. **Open a pull request** with `gh pr create`. Its title follows the same format, because a squash merge uses it as the commit title.
4. **CI** (`.github/workflows/ci.yml`) must pass before merging:

| Job | Runs | Pinned to |
|---|---|---|
| Commit messages | `gitlint` on the PR's commits and on its title (PRs only) | gitlint 0.19.1 |
| Native tests | `scripts/test.sh` with `BM_REQUIRE_STUB_TESTS=1`, so the stub tests cannot be skipped | xbyak v7.28 (vcpkg's version) |
| Formatting | `scripts/format.sh --check` | clang-format 23.1.0 (llvm-mingw's version) |

A second workflow, `.github/workflows/build.yml`, builds the DLL on pull requests and pushes to `master` that touch build inputs (`src/`, CMake, vcpkg, `lib/`, `scripts/`). The `master` builds fill the caches (sysroot, llvm-mingw, vcpkg packages) that pull requests read: GitHub shares caches with PRs only from the default branch. It runs `scripts/setup.sh` and `scripts/build.sh` exactly as a developer does, then `scripts/package.py`, and uploads the release archives as a downloadable artifact of the run. CI cannot run the game, so the in-game check stays part of every change that touches the plugin.

## Releasing

The version lives in one place: `VERSION` in `CMakeLists.txt`'s `project()` call. It ends up in the DLL and in the archive names.

1. Bump `VERSION` in a pull request and merge it.
2. Tag the merge commit and push the tag: `git tag v1.2.3 && git push origin v1.2.3`.
3. The build workflow refuses a tag that is not `v` + the version, then publishes a GitHub Release with both archives. It doesn't rebuild if a run in this repository already built the tag's exact tree: artifacts are named `BeyondMastery-tree-<tree hash>`, and a merged PR's own Build run usually matches, so the release ships the archives you could test from the PR. If `master` moved between that run and the merge, or the artifact expired (90 days), it builds instead. To get a reusable build after `master` moved, update the PR branch before merging. The archives:
   - `BeyondMastery-<version>.zip`, rooted at `Data/`, for Nexus and mod managers;
   - `BeyondMastery-<version>-SKSE-Plugins.zip`, rooted at `Data/SKSE/Plugins/`.

Both archives carry the DLL plus `BeyondMastery/LICENSE`, `README.md` and `THIRD-PARTY-NOTICES.txt`. The notices are generated from the build: the license of every vcpkg port it installed (`vcpkg_installed/<triplet>/share/<port>/copyright`) plus CommonLibSSE-NG's MIT and hde64 notices, because the MIT and BSD libraries linked into the DLL require them with every binary copy. A new dependency is covered automatically.

`python3 scripts/package.py` builds the same archives locally from `build/release/BeyondMastery.dll`, into `dist/`.

## Build

Development happens on a Linux host. The DLL is cross-compiled with clang-cl and lld-link against an xwin MSVC sysroot.

```bash
scripts/setup.sh    # one-time: tool check, CommonLibSSE-NG submodule, llvm-mingw, xwin sysroot, vcpkg
scripts/test.sh     # native tests only: fast, no cross-compiler needed
scripts/build.sh    # configure, run test.sh, build BeyondMastery.dll, check its runtime imports
scripts/format.sh   # clang-format in place; --check only reports (also VS Code tasks "format: check/fix")
```

`setup.sh` prints three environment variables, `VCPKG_ROOT`, `XWIN_SYSROOT` and `LLVM_MINGW_BIN`. Export them before `build.sh` will work. New shells don't inherit them.

The first build compiles CommonLibSSE-NG and every vcpkg port, so it takes a long time. Later builds are incremental, via Ninja in `build/release/`. There is one CMake preset, `linux-clangcl-release`:

```bash
cmake --preset linux-clangcl-release && cmake --build --preset release
```

The plugin links the C++ runtime **statically** (`/MT`), through the overlay triplet `cmake/triplets/x64-windows-clangcl-static.cmake`. Do not switch back to `/MD`. The player's `msvcp140.dll` can be arbitrarily old: the first in-game run met the 2016 runtime in the Proton prefix, and STL 14.40+ `std::mutex` faults on its first lock against it. `scripts/check_runtime_deps.py` fails the build if `msvcp140` or `vcruntime140` ever reappears in the import table.

To install a test build, copy `build/release/BeyondMastery.dll` to `<Skyrim>/Data/SKSE/Plugins/`. Do it only while the game is closed. The log is written to `Documents/My Games/Skyrim Special Edition/SKSE/BeyondMastery.log`.

## Tests

The C++ tests in `tests/` are self-contained binaries with `CHECK`/`CHECK_EQ`/`CHECK_NEAR` macros and a hand-written `main()`. There is no test framework. `scripts/test.sh` compiles each one with `c++ -std=c++20 -Wall -Wextra -Werror -I src`, entirely outside CMake. To add a test, add its name to that script's loop. The Python tests run under stdlib `unittest`.

| Test | Covers |
|---|---|
| `test_curves` | the math in `src/Curves.h` |
| `test_hooksites` | the site table and the runtime gate in `src/HookSites.h` |
| `test_patterns` | byte-signature matching in `src/Patterns.h` |
| `test_texts` | the state-dependent player text in `src/Texts.h` |
| `test_stubs` | the hand-written trampolines in `src/Stubs.h`, **emitted with xbyak and inspected**: frame arithmetic, registers, budget. It needs the xbyak headers from the vcpkg install, which is why `build.sh` configures before it tests. |
| `test_verify_sites.py` | Address Library parsing and site verification against the real `SkyrimSE.exe`; skipped when the exe is absent |
| `test_runtime_deps.py` | the build gate that keeps the plugin off the player's C++ runtime |

To run a single test, build and run it directly:

```bash
c++ -std=c++20 -I src tests/test_curves.cpp -o build/native/test_curves && build/native/test_curves
```

These tests are the only automated layer, and they can only cover the game-free headers: `Curves.h`, `HookSites.h`, `Patterns.h`, `Stubs.h` and `Texts.h`. Everything else needs the game. A cross-compiled DLL can also crash in-game on ABI or layout mismatches the compiler cannot see, so **in-game testing is mandatory** before claiming a change works.

## Layout

| Path | Content |
|---|---|
| `src/Curves.h` | Pure math: costs, gates and limits, gem planning, XP factor, form A/B curves |
| `src/HookSites.h` | Every patch site: Address Library IDs, offsets and byte patterns per runtime, plus the runtime gate |
| `src/Patterns.h` | Byte-signature matching |
| `src/Stubs.h` | The hand-written xbyak trampolines |
| `src/Texts.h` | Player-facing text that depends on state |
| `src/Hooks.*` | Resolves and checks every site, installs the hooks, and holds the cap and XP-gain callees |
| `src/Menu.*` | The Legendary menu: Reset Skill, Upgrade, Cancel |
| `src/Scaling.*` | Formula scaling above 100: the remap, the clamp, the skills-menu bypass, enchanting charge, barter |
| `src/Resources.*` | Reads and spends perks, dragon souls and filled black soul gems |
| `src/State.*` | Per-skill caps and paid gates, with co-save serialization |
| `src/Skills.h` | Skill ↔ actor value mapping, and the scaling form per skill |
| `src/Config.*` | INI settings |
| `src/main.cpp` | SKSE entry point |
| `tests/` | Native unit tests |
| `scripts/` | Setup, build, test, format, site verification, runtime-import gate |
| `lib/commonlibsse-ng` | CommonLibSSE-NG submodule |

## Architecture

### Game-free core

`src/Curves.h`, `HookSites.h`, `Patterns.h`, `Stubs.h` and `Texts.h` are header-only, so `scripts/test.sh` can compile them standalone. Keep them free of `RE/`, `SKSE/` and `PCH.h` includes and of C++23-only features (`Stubs.h` may use xbyak). The plugin itself builds as C++23 with `src/PCH.h` force-included. **New math goes in `Curves.h`, with a test, not inline in a hook.**

`PowerDecayFormula` deliberately offers both `Factor()`, for computing the value ourselves, and `EffectiveSkill()`, which inverts the vanilla formula so the engine computes the right value from a faked skill. Which one a hook uses depends on whether it replaces the engine's calculation or feeds it a remapped skill.

### Formula scaling (PRD §6.9, hook-points.md §5, §6)

Instead of one hook per skill-dependent formula, **slot 1 of the player's ActorValueOwner vtable** (`GetActorValue`) returns a remapped effective skill `s_eff` for skills above 100. `src/Skills.h` maps each of the 18 skills to its `Scaling` form (`kSqrt`, `kSpellCost` or `kBarter`), and `ScalingOf()` is the single place that decides. The callees live in `src/Scaling.*`; `Hooks.cpp` installs them, all or nothing. NPCs stay vanilla: the remap is in the player's own vtable.

Consequences:

- Every other reader of the player's current skill also sees `s_eff`: Papyrus, perk conditions and other mods. The values stay ≥ 100, so vanilla perk requirements still pass. The XP routine reads the *base* value (slot 3), so leveling is unaffected.
- The engine's generic `GetClampedActorValue` clamps every skill to 100. Spell cost, barter Speech, Lockpicking, Pickpocket and Sneak read through it, so it is replaced: vanilla for everyone, but the player's skills are only kept ≥ 0.
- Barter gets form B through `s_eff` itself: Speech's remap inverts the vanilla formula, so the engine computes the asymptotic value.
- Spell cost gets form B in its own two calls. The remap is form A for every other skill, and a modded effect can tie spell cost to any of them, so the wrapper recovers the real skill from form A, applies form B, and hands the engine the skill that produces that factor.
- The skills menu must show the real level (FR-29): its three calls to `UpdateSkillList` are wrapped so the remap is off while it runs, and the crafting menus' bottom-bar read calls the real value directly.
- Enchanting charge per use is the exception: its only call is replaced and computed from the **real** skill (FR-30), while its magnitude goes through `s_eff`.
- Barter's final clamp after perks is replaced by a stub that adds the 1.2 buy/sell floor (FR-26).

### Plugin units

- `main.cpp`: the `SKSEPluginLoad` entry point. It runs init, the runtime gate, config load and serialization registration, and sets up a messaging listener. That listener detects conflicting SKSE plugins at `kPostLoad` and notifies the player once at `kDataLoaded` (FR-20).
- `Config`: a singleton read from `Data/SKSE/Plugins/BeyondMastery.ini` via SimpleIni. `Load()` validates and clamps every value, then **writes the whole file back** so that new keys appear for users after an update. A new setting therefore goes in both the read block and the write-back block, with its comment string.
- `State`: a singleton holding each skill's cap and paid gates. It persists in the SKSE co-save under unique ID `'BMST'`, as record `'CAPS'` v1 and record `'GATE'` v1. The loader is defensive by contract (FR-22): an unknown record, a wrong version or length, or an implausible value falls back to 100 rather than failing. A save without a `GATE` record gets its gates derived from its caps. Changing a record's layout means bumping its version and handling the old one in `OnLoad`.
- `Menu`: replaces the call that builds vanilla's Legendary box (hook-points.md §7.1). Reset Skill hands control back to the vanilla callback, so vanilla's second confirmation still appears, with its text replaced (FR-31).

## Working on hooks

- Resolve addresses through Address Library IDs only, never hardcoded offsets (PRD §7). SE and AE IDs differ, and they often need separate wrappers because the surrounding instructions differ.
- Check the byte signature at every patch site before writing. On a mismatch, log and skip rather than patching blind. A failed signature check is also the second signal for conflict detection (hook-points.md §8).
- Don't install a hook that the current config makes a no-op. Examples: the skill XP gain hook while `bScaleXPGainAbove100` is off, and the character-XP hook while `bClampCharXPAbove100` is false. Fewer hooks means fewer conflicts.
- Treat IDs and offsets from hook-points.md as *facts about the binary to re-verify*, not as working code. The Rust uncapper referenced there has no license file, so nothing may be copied from it.
- Derive formula constants from the disassembly, or from GMSTs read at runtime, not from UESP values.
- Reach any member of a game structure whose layout differs between runtimes through CommonLibSSE-NG's runtime accessors: `RUNTIME_*_ACCESSOR*` (e.g. `AsActorValueOwner()`), `GetXRuntimeData()` or `REL::RelocateMember*`. Never use a hardcoded offset or a direct member. The plugin declares `StructCompatibility::Independent`, and that claim only holds because one binary resolves every such offset from the running game's version.

## License constraint

The project is GPL-3.0-or-later (see [README.md](../README.md#license)), and its source must be distributable. Don't add dependencies with incompatible licenses.
