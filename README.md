# Beyond Mastery

SKSE plugin for Skyrim AE 1.6.x and 1.7.x: raise each skill's cap above 100 in +10 steps, paid with perk points, dragon souls and filled black soul gems. Skill effects above 100 continue sub-linearly or asymptotically.

- Download: [Nexus Mods](https://www.nexusmods.com/skyrimspecialedition/mods/196536) or the [GitHub releases](https://github.com/1990jjhh-ctrl/Beyond-Mastery/releases)
- Installing, uninstalling and what players see, as on the Nexus page: [docs/nexus/description.md](docs/nexus/description.md#installation-instructions)
- Requirements: [PRD.md](PRD.md)
- Testing, releasing, contributing and the source layout: [docs/development.md](docs/development.md)
- How the engine patch sites were found: [docs/hook-points.md](docs/hook-points.md)

## Setup and build

Development happens on a Linux host. The DLL is cross-compiled with clang-cl and lld-link against an xwin MSVC sysroot.

The tools, CommonLibSSE-NG and the vcpkg ports come from the [skyrim-mod-toolchain](lib/skyrim-mod-toolchain/README.md) submodule (`git submodule update --init --recursive`). Its README explains [how a project uses it](lib/skyrim-mod-toolchain/README.md#use-it-in-a-project).

```bash
lib/skyrim-mod-toolchain/scripts/setup.sh                 # one-time toolchain setup (see its README above)
scripts/test.sh                                           # native tests only: fast, no cross-compiler needed
scripts/build.sh                                          # configure, run test.sh, build BeyondMastery.dll, check its runtime imports
lib/skyrim-mod-toolchain/scripts/format.sh src tests      # clang-format in place; --check only reports (also VS Code tasks "format: check/fix")
lib/skyrim-mod-toolchain/scripts/lint.sh build/release src  # clang-tidy against the build's compile database
```

`setup.sh` prints the environment variables `build.sh` needs; export them, since new shells don't inherit them. The first build compiles CommonLibSSE-NG and every vcpkg port and takes a long time; later builds are incremental in `build/release/`.

The plugin links the C++ runtime statically. Don't switch it back to the dynamic runtime: [`check_runtime_deps.py`](lib/skyrim-mod-toolchain/scripts/check_runtime_deps.py) explains why and fails the build if it happens.

The MCM's Papyrus scripts in `papyrus/` are compiled locally and the `.pex` files are committed in `papyrus/compiled/`, because CI can't run the Creation Kit's compiler. After changing a `.psc`, run `scripts/papyrus.sh` and commit the result:

```bash
SKYRIM_DIR=<Skyrim SE folder> PROTON_DIR=<Proton Experimental>/files scripts/papyrus.sh
```

It fetches SkyUI's headers, then runs the toolchain's [Papyrus compile](lib/skyrim-mod-toolchain/README.md#papyrus-compile-papyrussh), whose README says what it needs. Install the Creation Kit (Steam app 1946180) into `SKYRIM_DIR` and unpack its `Scripts.zip` into `Data/`, which gives the compiler's `Data/Source/Scripts`.

To test a build, copy `build/release/BeyondMastery.dll` to `<Skyrim>/Data/SKSE/Plugins/` while the game is closed. The log is `Documents/My Games/Skyrim Special Edition/SKSE/BeyondMastery.log`.

## License

GPL-3.0-or-later (CommonLibSSE-NG is GPL-3.0-or-later and statically linked).
