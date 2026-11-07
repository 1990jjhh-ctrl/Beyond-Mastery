# Move Beyond Mastery onto skyrim-mod-toolchain: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Beyond Mastery builds, tests, formats, lints and releases through the shared toolchain, and keeps no copy of the toolchain's setup, triplet, runtime check, format script, style config or CI install steps.

**Architecture:** [skyrim-mod-toolchain](https://github.com/1990jjhh-ctrl/skyrim-mod-toolchain) `v0.2.0` becomes the submodule `lib/skyrim-mod-toolchain`. It replaces the `lib/commonlibsse-ng` submodule, because the toolchain carries CommonLibSSE-NG v13.1.0, the same version as today. The project symlinks the toolchain's `vcpkg-configuration.json`, `.clang-format` and `.clang-tidy`, includes its presets and CMake helper, and calls its scripts. Usage is in the toolchain's README, section "Use it in a project". Its `tests/consumer` is a working example.

**Tech Stack:** CMake presets, vcpkg, Bash, Python `unittest`, GitHub Actions.

**Spec:** the toolchain's PRD, R13 (milestone 3), at https://github.com/1990jjhh-ctrl/skyrim-mod-toolchain/blob/v0.2.0/docs/PRD.md, and this project's [docs/development.md](../../development.md) for its workflow. Decisions the user made for milestone 3:
- The shared code style is trials-of-mastery's (the toolchain's `.clang-format`), so `src/` and `tests/` get reformatted.
- CI runs both the format check and lint.
- The release workflow uses the toolchain's setup action.

## Global Constraints

- Branch `build/skyrim-mod-toolchain` from `master`. The work lands through one pull request, which is squash-merged, so its title must be a valid Conventional Commit (`.gitlint`).
- The toolchain is pinned to tag `v0.2.0` (commit `d34a05c`).
- Nothing the toolchain owns stays in this repo. Delete `scripts/setup.sh`, `scripts/check_runtime_deps.py`, `tests/test_runtime_deps.py`, `scripts/format.sh` and `cmake/triplets/`, and replace `.clang-format` with a symlink. Project-specific scripts stay: `build.sh`, `test.sh`, `papyrus.sh`, `package.py`, `make_esp.py`, `normalize_pex.py` and `verify_sites.py`.
- Behaviour doesn't change. Formatting and lint fixes are pure refactors in their own commits. The plugin's runtime support stays as today: SE and AE on, VR off, and `SKSE_SUPPORT_XBYAK` on.
- `docs/nexus/images/MCM.png` is the user's uncommitted file. Never add, move or delete it.
- Follow the CLAUDE.md conventions: Conventional Commits, small single-purpose commits, and fixes folded in with `--fixup`.

## Review Focus

1. **Paths into CommonLibSSE-NG.** `tests/test_verify_sites.py`, `tests/test_package.py`, `scripts/package.py` and a comment in `scripts/verify_sites.py` read `lib/commonlibsse-ng/...`. After the move they must read `lib/skyrim-mod-toolchain/lib/commonlibsse-ng/...`. A miss makes the REL fixture tests skip, or makes packaging lose the third-party notices. Test: Task 1 Step 4 runs the full `scripts/test.sh` and greps its output for `skipped`. Task 1 Step 5 runs `package.py`.
2. **Build directory name.** `package.py`, `build.sh` and `test.sh` (the xbyak lookup) expect `build/release`. The shared preset's `binaryDir` is `build/${presetName}`, so the project's configure preset stays named so that the directory is still `build/release`, or overrides `binaryDir`. Test: Task 1 Step 5.
3. **Release workflow.** The tag check, the reuse of an earlier build and the publish steps in `build.yml` must keep working. Only the toolchain install and caching change. Test: Task 4 Step 3 runs the PR build. The tag path is checked by reading the diff, because a release isn't cut.
4. **Lint findings fixed as refactors.** Each fix keeps behaviour, and the native tests and the in-game-free checks still pass. Test: Task 2 Step 4.

---

### Task 1: Toolchain submodule, build files and paths

**Files:**
- Add: submodule `lib/skyrim-mod-toolchain` at `v0.2.0`.
- Remove: submodule `lib/commonlibsse-ng` (`.gitmodules` entry and gitlink) and `cmake/triplets/`.
- Symlinks: `vcpkg-configuration.json`, `.clang-format` and `.clang-tidy`, each pointing to `lib/skyrim-mod-toolchain/<same name>`.
- Modify: `CMakePresets.json`, `CMakeLists.txt:16-36`, `scripts/build.sh`, `scripts/package.py`, `scripts/verify_sites.py` (comment), `tests/test_package.py`, `tests/test_verify_sites.py`, `.github/workflows/ci.yml` (the comment that names the REL fixture path).
- Delete: `scripts/setup.sh`, `scripts/check_runtime_deps.py`, `tests/test_runtime_deps.py`.

**Interfaces:**
- `CMakePresets.json` has `"include": ["lib/skyrim-mod-toolchain/cmake/CMakePresets.json"]`. Configure preset `release` inherits `skse-clangcl` and adds `USE_VCPKG`, if the shared preset doesn't set it, plus nothing else the shared preset already sets. Build preset `release`. `build/release` stays the build directory.
- `CMakeLists.txt` sets `ENABLE_SKYRIM_*`, `SKSE_SUPPORT_XBYAK` and `BUILD_TESTS` as today, then `include(lib/skyrim-mod-toolchain/cmake/commonlibsse.cmake)`, which replaces the `add_subdirectory` and `include` pair.
- `scripts/build.sh` runs `cmake --preset release` and calls `lib/skyrim-mod-toolchain/scripts/check_runtime_deps.py`. Its hints say `lib/skyrim-mod-toolchain/scripts/setup.sh`.

- [ ] **Step 1:** Make the submodule and file changes above. `git submodule deinit` and `git rm` the old submodule.
- [ ] **Step 2:** Update every `lib/commonlibsse-ng` path in project files: run `grep -rn 'lib/commonlibsse-ng' --exclude-dir=lib --exclude-dir=build .`. Leave historical plan files under `docs/superpowers/` as they are.
- [ ] **Step 3:** Remove `verify_sites.py`'s dependency on the deleted `check_runtime_deps.py`, if it has one. The project's `PEFile` stays.
- [ ] **Step 4:** Run `scripts/test.sh` on its own. Expected: exit 0. The REL fixture tests in `test_verify_sites.py` run (no "skipped" for missing fixtures), and the stub tests run once `build/` holds xbyak.
- [ ] **Step 5: Real build.**

  ```bash
  export VCPKG_ROOT=$HOME/.local/share/vcpkg XWIN_SYSROOT=$HOME/xwin-out LLVM_MINGW_BIN=$HOME/.local/opt/llvm-mingw/bin
  rm -rf build/release
  scripts/build.sh
  ```

  Run it in the background, because the first configure builds the vcpkg ports. Expected: `build/release/BeyondMastery.dll`, and the runtime check prints `OK`. Then run `python3 scripts/package.py --out dist`. Expected: it succeeds, and the third-party notices include CommonLibSSE-NG's licenses.
- [ ] **Step 6: Commit,** in the order given:
  1. `build: take CommonLibSSE-NG and the build setup from skyrim-mod-toolchain`: the submodules, symlinks, presets, CMake and paths.
  2. `build: drop the setup script and runtime check the toolchain provides`: the deletions and the `build.sh` call.

### Task 2: Shared style, format and lint

**Files:**
- Modify: every `*.h`/`*.cpp` under `src/` and `tests/` that the formatter or lint touches, and `.vscode/tasks.json`.
- Delete: `scripts/format.sh`.

- [ ] **Step 1:** Run `lib/skyrim-mod-toolchain/scripts/format.sh src tests` (llvm-mingw's clang-format). Commit only the formatting as `style: reformat to the shared skyrim-mod-toolchain style`.
- [ ] **Step 2:** Delete `scripts/format.sh`. Point `.vscode/tasks.json`'s format tasks at `lib/skyrim-mod-toolchain/scripts/format.sh` with arguments `src tests`. Add a `lint` task that runs `lib/skyrim-mod-toolchain/scripts/lint.sh build/release src`. Commit as `build: format and lint with the toolchain's scripts`.
- [ ] **Step 3:** Run `lib/skyrim-mod-toolchain/scripts/lint.sh build/release src` (this needs Task 1's build). Fix every finding without changing behaviour: early returns, and extracting helpers to reduce nesting. Commit per file or per finding kind, as `refactor: …` with a concrete title.
- [ ] **Step 4:** Re-run `format.sh --check src tests` and `lint.sh build/release src` (both clean), then `scripts/test.sh` and `scripts/build.sh` (both green, runtime check `OK`).

### Task 3: CI and docs

**Files:**
- Modify: `.github/workflows/ci.yml`, `.github/workflows/build.yml`, `docs/development.md` (and README.md only if it mentions setup).

**Interfaces:**
- `build.yml`'s `build` job:
  1. `actions/checkout` with `submodules: recursive`.
  2. `uses: ./lib/skyrim-mod-toolchain/.github/actions/setup`.
  3. `scripts/build.sh`.
  4. `format.sh --check src tests`, then `lint.sh build/release src`.
  5. `package.py` and the upload, unchanged.

  Remove the job's own `System tools`, `xwin` and three cache steps, and the `LLVM_VERSION`/`XWIN_VERSION` env lines and the comments about them. Add `tests/**` and `.clang-format` to the path filters, so a formatting change triggers the job. The `prepare` and `release` jobs are unchanged.
- `ci.yml`: delete the `format` job, since format now runs in `build.yml`. The `tests` job keeps the native tests. It needs the REL fixtures, which now sit in the nested submodule, so it uses `submodules: recursive`.

- [ ] **Step 1:** Edit both workflows as above. Keep the repo's existing action versions (`actions/checkout@v4` and so on) unless the setup action needs newer ones.
- [ ] **Step 2:** In `docs/development.md`:
  - Replace the setup and format commands with the toolchain's: `lib/skyrim-mod-toolchain/scripts/setup.sh`, `format.sh src tests` and `lint.sh build/release src`.
  - Link the toolchain README's "Use it in a project" section instead of repeating it.
  - Update the CI paragraph (format and lint run in `build.yml`, through the setup action) and the `lib/` table row.
  - Keep `check_runtime_deps.py`'s explanation as a link.
- [ ] **Step 3:** Check that every relative link resolves. Commit as `ci: build, format and lint through the toolchain's setup action`, then `docs: describe the toolchain-based setup`.

### Task 4: Verify on GitHub

- [ ] **Step 1:** Push the branch and open a pull request against `master` with `gh pr create`. The title is `build: move onto skyrim-mod-toolchain` (squash-merge title). The body lists the tasks and ends with the Claude Code line.
- [ ] **Step 2:** Watch the runs. Expected: `ci.yml` (commits and tests) and `build.yml` (build, format, lint, package) are green, and the archives artifact is uploaded. Record the durations.
- [ ] **Step 3:** If the build job fails, fix with a `--fixup` of the responsible commit and a plain push. Never force-push, merge or tag; merging and releasing are the user's decision.
- [ ] **Step 4:** In the PR body, state that the DLL was rebuilt from reformatted and lint-refactored sources and needs the mandatory in-game check (docs/development.md, "Tests") before merging. Attach the CI artifact's name so the user can test exactly that build.
