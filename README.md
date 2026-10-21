# Beyond Mastery

SKSE plugin for Skyrim SE 1.5.97 and AE 1.6.x: raise each skill's cap above 100 in +10 steps, paid with perk points, dragon souls and filled black soul gems. Skill effects above 100 continue sub-linearly or asymptotically.

- Requirements: [PRD.md](PRD.md)
- Engine hook design: [docs/hook-points.md](docs/hook-points.md)

## Status

**Milestone 1 (this scaffold):** the plugin loads, writes `BeyondMastery.log`, reads/writes `BeyondMastery.ini`, persists per-skill caps in the SKSE co-save, and warns about conflicting plugins. It does not change gameplay yet.

Next: verify the hook sites in Ghidra (hook-points.md §9), then add hooks, the lesser power and the menu.

## Layout

| Path | Content |
|---|---|
| `src/Curves.h` | Pure math: costs, XP factor, form A/B curves. Unit-tested natively. |
| `src/Config.*` | INI settings |
| `src/State.*` | Per-skill caps, co-save serialization |
| `src/Skills.h` | Skill ↔ actor value mapping, scaling form per skill |
| `src/main.cpp` | SKSE entry point |
| `tests/` | Native unit tests |
| `lib/commonlibsse-ng` | CommonLibSSE-NG submodule (added by `scripts/setup.sh`) |

## Build (Linux host, cross-compile)

```bash
scripts/setup.sh   # one-time: checks tools, adds submodule, xwin sysroot, vcpkg
scripts/build.sh   # native tests + BeyondMastery.dll
```

The first build compiles CommonLibSSE-NG and all vcpkg dependencies and takes a while.

Native tests only: `scripts/test.sh`

A cross-compiled DLL can still crash in-game on ABI/layout mismatches the compiler can't see. Always test in-game.

## Install (testing)

Copy `build/release/BeyondMastery.dll` to `Data/SKSE/Plugins/`. Requires SKSE64 and Address Library for SKSE Plugins.

## License

GPL-3.0-or-later (CommonLibSSE-NG is GPL-3.0-or-later and statically linked).
