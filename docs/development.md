# Development

How Beyond Mastery is built, tested, released and put together. What the mod does is in [PRD.md](../PRD.md); installing and the project's purpose are in [README.md](../README.md).

## Documents

- [PRD.md](../PRD.md): the approved requirements, numbered `FR-n`, and the acceptance results (§8). Read it before changing behavior. Code and tests cite the IDs in comments, so keep the citations accurate when either side changes.
- [hook-points.md](hook-points.md): how the engine patch sites were found, and the evidence behind them. The sites themselves are in `src/HookSites.h`.
- [nexus/description.md](nexus/description.md): the player-facing text. It is the Nexus page, and `scripts/package.py` ships it as the readme in the release archives. It is the one deliberate restatement of the PRD, written for players, so a pull request that changes behavior updates it too.
- [superpowers/plans/](superpowers/plans/): the implementation plans, with the reasoning behind what was built and what was left out. They are records, not current documentation.

## Contributing

Every change goes through a branch and a pull request; nothing is committed to `master` directly.

1. **Branch** from `master`, named after the change's type: `feat/…`, `fix/…`, `docs/…`, `ci/…`.
2. **Commit** with [Conventional Commits](https://www.conventionalcommits.org) titles. The exact rules (types, line lengths) are in `.gitlint`. Run `gitlint install-hook` once per clone, so a bad message is rejected when you commit rather than in CI.
3. **Open a pull request** with `gh pr create`. Its title follows the same format, because a squash merge uses it as the commit title.
4. **CI** must pass before merging. `.github/workflows/ci.yml` lints the commit messages and the PR title, runs `scripts/test.sh` (stub tests required), and checks formatting with `scripts/format.sh --check`. Tool versions are pinned there to match the local ones.

`.github/workflows/build.yml` builds the DLL on pull requests and pushes to `master` that touch build inputs. It runs `scripts/setup.sh`, `scripts/build.sh` and `scripts/package.py` exactly as a developer does, and uploads the release archives as an artifact of the run. Its header explains the caching and how a tag reuses an earlier build. CI cannot run the game, so the in-game check stays part of every change that touches the plugin.

## Releasing

The version is `VERSION` in `CMakeLists.txt`'s `project()` call, and nowhere else.

1. Bump it in a pull request and merge it.
2. Tag the merge commit and push the tag: `git tag v1.2.3 origin/master && git push origin v1.2.3`.
3. The build workflow refuses a tag that doesn't match the version, then publishes a GitHub Release with the archives (`scripts/package.py` describes them). If the PR's or `master`'s build of the exact same tree is still available, it's reused instead of rebuilt.

`python3 scripts/package.py` builds the same archives locally, into `dist/`.

## Build

Development happens on a Linux host. The DLL is cross-compiled with clang-cl and lld-link against an xwin MSVC sysroot.

```bash
scripts/setup.sh    # one-time: tools, CommonLibSSE-NG submodule, llvm-mingw, xwin sysroot, vcpkg
scripts/test.sh     # native tests only: fast, no cross-compiler needed
scripts/build.sh    # configure, run test.sh, build BeyondMastery.dll, check its runtime imports
scripts/format.sh   # clang-format in place; --check only reports (also VS Code tasks "format: check/fix")
```

`setup.sh` prints the environment variables `build.sh` needs; export them, since new shells don't inherit them. The first build compiles CommonLibSSE-NG and every vcpkg port and takes a long time; later builds are incremental in `build/release/`.

The plugin links the C++ runtime statically. Don't switch it back to the dynamic runtime: `scripts/check_runtime_deps.py` explains why and fails the build if it happens.

To test a build, copy `build/release/BeyondMastery.dll` to `<Skyrim>/Data/SKSE/Plugins/` while the game is closed. The log is `Documents/My Games/Skyrim Special Edition/SKSE/BeyondMastery.log`.

## Tests

`scripts/test.sh` runs every test: the C++ binaries in `tests/` (self-contained, no framework; add a new one to the script's loop) and the Python tests under stdlib `unittest`. Each test file starts with a comment saying what it covers.

They can only cover code that runs without the game: the headers `Curves.h`, `HookSites.h`, `Patterns.h`, `Stubs.h` and `Texts.h`, and the scripts. Everything else needs the game, and a cross-compiled DLL can crash on ABI or layout mismatches the compiler cannot see. **In-game testing is mandatory** before claiming a change works.

## Layout

Each source file starts with a comment describing it. As a map:

| Path | Content |
|---|---|
| `src/Curves.h` | The math |
| `src/HookSites.h` | Every patch site |
| `src/Patterns.h`, `src/Stubs.h` | Byte-signature matching, the hand-written trampolines |
| `src/Hooks.*` | Checking and installing every hook |
| `src/Scaling.*` | Formula scaling above 100 |
| `src/Menu.*`, `src/Texts.h` | The Legendary menu and its text |
| `src/Resources.*` | Perks, dragon souls, soul gems |
| `src/State.*` | Caps and gates in the co-save |
| `src/Compat.*` | Plugins Beyond Mastery adapts to (Experience) |
| `src/Skills.h`, `src/Config.*`, `src/main.cpp` | Skill mapping, INI settings, entry point |
| `tests/`, `scripts/` | Tests; setup, build, packaging and verification scripts |
| `lib/commonlibsse-ng` | CommonLibSSE-NG submodule |

## Rules for code

- **Game-free headers stay game-free.** The headers listed under Tests must not include `RE/`, `SKSE/` or `PCH.h`, and must stay C++20, because `scripts/test.sh` compiles them standalone. New math goes in `Curves.h`, with a test, not inline in a hook.
- **Addresses come from Address Library IDs only**, never hardcoded offsets (PRD §7).
- **Check the bytes before writing.** Every patch site is byte-checked, and call sites are also checked for their target. On a mismatch, log and skip rather than patching blind; a mismatch is also how a conflicting mod is detected.
- **Don't install a hook the configuration makes a no-op**, e.g. an optional feature that is switched off. Fewer hooks means fewer conflicts.
- **Facts from other mods are claims to re-verify.** IDs, offsets and patterns taken from another mod (see hook-points.md, "Sources") must be confirmed in the disassembly. Nothing may be copied from a mod without a license.
- **Formula constants come from the disassembly or from GMSTs read at runtime**, not from wiki values.
- **Members whose layout differs between runtimes are reached through CommonLibSSE-NG's runtime accessors** (`RUNTIME_*_ACCESSOR*`, `GetXRuntimeData()`, `REL::RelocateMember*`), never a hardcoded offset or a direct member. The plugin's `StructCompatibility::Independent` claim depends on it.
- **Dependencies must be GPL-compatible**, since the project is GPL-3.0-or-later (see README.md).
