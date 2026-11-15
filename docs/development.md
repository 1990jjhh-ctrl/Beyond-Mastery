# Development

How Beyond Mastery is tested, released and put together. What the mod does is in [PRD.md](../PRD.md); the project's purpose and how to set it up and build it are in [README.md](../README.md), installing the mod is in [nexus/description.md](nexus/description.md#installation-instructions).

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
4. **CI** must pass before merging. `.github/workflows/ci.yml` lints the commit messages and the PR title, and runs `scripts/test.sh` (stub tests required). Tool versions are pinned there to match the local ones.

`.github/workflows/build.yml` builds the DLL on pull requests and pushes to `master` that touch build inputs. It installs the toolchain through the [setup action](../lib/skyrim-mod-toolchain/.github/actions/setup/action.yml), then runs `scripts/build.sh`, the format check, lint and `scripts/package.py` as a developer does, and uploads the release archives as an artifact of the run. Its header explains how a tag reuses an earlier build; caching is done by the setup action. CI cannot run the game, so the in-game check stays part of every change that touches the plugin.

## Releasing

The version is `VERSION` in `CMakeLists.txt`'s `project()` call, and nowhere else.

1. Bump it in a pull request and merge it.
2. Tag the merge commit and push the tag: `git tag v1.2.3 origin/master && git push origin v1.2.3`.
3. The build workflow refuses a tag that doesn't match the version, then publishes a GitHub Release with the archives (`scripts/package.py` describes them). If the PR's or `master`'s build of the exact same tree is still available, it's reused instead of rebuilt.

`python3 scripts/package.py` builds the same archives locally, into `dist/`.

## Tests

`scripts/test.sh` runs every test: the C++ binaries in `tests/` (self-contained, no framework; add a new one to the script's loop) and the Python tests under stdlib `unittest`. Each test file starts with a comment saying what it covers.

They can only cover code that runs without the game: the headers `Curves.h`, `CustomSkillsData.h`, `CustomSkillsSites.h`, `HookSites.h`, `Patterns.h`, `Stubs.h` and `Texts.h`, and the scripts. `tests/test_customskills_sites.py` also checks `CustomSkillsSites.h` against real Custom Skills Framework `CustomSkills.dll` files, one per supported build, when `BM_CSF_DLL` lists them (separated by `:`), and skips that part otherwise. Everything else needs the game, and a cross-compiled DLL can crash on ABI or layout mismatches the compiler cannot see. **In-game testing is mandatory** before claiming a change works.

## Layout

Each source file starts with a comment describing it. As a map:

| Path | Content |
|---|---|
| `src/Curves.h` | The math |
| `src/HookSites.h` | Every patch site |
| `src/Patterns.h`, `src/Stubs.h` | Byte-signature matching, the hand-written trampolines |
| `src/Hooks.*` | Checking and installing every hook |
| `src/Scaling.*` | Formula scaling above 100 |
| `src/CustomSkills.*` | Custom Skills Framework skills: discovery from CSF's JSON files, opt-in settings, level limit |
| `src/CustomSkillsData.h`, `src/CustomSkillsSites.h` | Game-free: the per-skill INI settings, the CCAP co-save encoding; CSF 3.2.0 patch sites |
| `src/Papyrus.*` | The native functions the MCM's scripts call |
| `src/Menu.*`, `src/Texts.h` | The Legendary menu and its text |
| `src/Resources.*` | Perks, dragon souls, soul gems |
| `src/State.*` | Caps and gates in the co-save |
| `src/Compat.*` | Plugins Beyond Mastery adapts to (Experience, Custom Skills Framework) |
| `src/Skills.h`, `src/Config.*`, `src/main.cpp` | Skill mapping, INI settings, entry point |
| `papyrus/` | The MCM's Papyrus sources and, in `compiled/`, the committed `.pex`; `BeyondMastery.toml` describes its ESL plugin for the toolchain's `esp.py` |
| `tests/`, `scripts/` | Tests; build, packaging and verification scripts |
| `lib/skyrim-mod-toolchain` | Submodule: setup, format and lint scripts, the plugin and Papyrus tools, the CI setup action, and CommonLibSSE-NG |

## Rules for code

- **Game-free headers stay game-free.** The headers listed under Tests must not include `RE/`, `SKSE/` or `PCH.h`, and must stay C++20, because `scripts/test.sh` compiles them standalone. New math goes in `Curves.h`, with a test, not inline in a hook.
- **Addresses come from Address Library IDs only**, never hardcoded offsets (PRD §7).
- **Check the bytes before writing.** Every patch site is byte-checked, and call sites are also checked for their target. On a mismatch, log and skip rather than patching blind; a mismatch is also how a conflicting mod is detected.
- **Don't install a hook the configuration makes a no-op**, e.g. an optional feature that is switched off. Fewer hooks means fewer conflicts.
- **Facts from other mods are claims to re-verify.** IDs, offsets and patterns taken from another mod (see hook-points.md, "Sources") must be confirmed in the disassembly. Nothing may be copied from a mod without a license.
- **Formula constants come from the disassembly or from GMSTs read at runtime**, not from wiki values.
- **Members whose layout differs between runtimes are reached through CommonLibSSE-NG's runtime accessors** (`RUNTIME_*_ACCESSOR*`, `GetXRuntimeData()`, `REL::RelocateMember*`), never a hardcoded offset or a direct member. The plugin's `StructCompatibility::Independent` claim depends on it.
- **Dependencies must be GPL-compatible**, since the project is GPL-3.0-or-later (see [README.md](../README.md#license)).
