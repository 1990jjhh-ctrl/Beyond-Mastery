# Beyond Mastery

SKSE plugin for Skyrim AE 1.6.x and 1.7.x: raise each skill's cap above 100 in +10 steps, paid with perk points, dragon souls and filled black soul gems. Skill effects above 100 continue sub-linearly or asymptotically.

- Requirements: [PRD.md](PRD.md)
- Engine hook design: [docs/hook-points.md](docs/hook-points.md)

## Status

Supported runtimes: AE 1.6.x and 1.7.x. SE 1.5.97 is out of scope for now
(PRD §3): the plugin loads there but stays inactive.

Working: the plugin loads, writes `BeyondMastery.log`, reads and writes
`BeyondMastery.ini`, persists per-skill caps in the SKSE co-save, warns once
about conflicting plugins, and resolves all nine engine patch sites through the
Address Library with a byte-signature check before touching anything. All nine
were verified offline against a real `SkyrimSE.exe` — run
`scripts/verify_sites.py` to repeat it ([docs/hook-points.md](docs/hook-points.md) §9a).

Cap enforcement is installed on AE and 1.7.x and has been run in-game on
1.7.104: skills level normally and stop at their cap. Every cap is still 100
until the cap-raise transaction exists, so in practice it currently plays
exactly like vanilla; a raised cap has not been tested yet.

Not yet implemented: the cap-raise transaction and its menu (so caps cannot be
raised in-game yet) and formula scaling above 100.

## Uninstalling

Skills already above 100 keep the level they reached — the engine stores skill
levels as floats, so nothing resets — but they stop progressing once the plugin
is gone, and nothing above 100 will rise again.

Reinstalling with the co-save intact restores every cap. Reinstalling *without*
it (a new save, or a co-save that was deleted) puts every cap back to 100 while
those skills keep their higher levels; they simply sit above their cap and stop
earning XP until the cap is raised past them again. No resource you already
spent is refunded.

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
