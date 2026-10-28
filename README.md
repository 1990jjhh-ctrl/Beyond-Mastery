# Beyond Mastery

SKSE plugin for Skyrim AE 1.6.x and 1.7.x: raise each skill's cap above 100 in +10 steps, paid with perk points, dragon souls and filled black soul gems. Skill effects above 100 continue sub-linearly or asymptotically.

- Download: [Nexus Mods](https://www.nexusmods.com/skyrimspecialedition/mods/196536) or the [GitHub releases](https://github.com/1990jjhh-ctrl/Beyond-Mastery/releases)
- Requirements: [PRD.md](PRD.md)
- What players see, as on the Nexus page: [docs/nexus/description.md](docs/nexus/description.md)
- Building, testing and contributing: [docs/development.md](docs/development.md)
- How the engine patch sites were found: [docs/hook-points.md](docs/hook-points.md)

## Install

Requires [SKSE64](https://www.nexusmods.com/skyrimspecialedition/mods/30379?tab=files)
and [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444).

Each [release](https://github.com/1990jjhh-ctrl/Beyond-Mastery/releases) has two
archives with the same files:

- `BeyondMastery-<version>.zip`: install it with a mod manager (Mod Organizer 2,
  Vortex), or extract it into `Data/`.
- `BeyondMastery-<version>-SKSE-Plugins.zip`: extract it straight into
  `Data/SKSE/Plugins/`.

Neither contains `BeyondMastery.ini`: the plugin writes it on first start, and
an update keeps your settings.

## Uninstalling

Skills already above 100 keep the level they reached — the engine stores skill
levels as floats, so nothing resets — but they stop progressing once the plugin
is gone, and nothing above 100 will rise again.

Reinstalling with the co-save intact restores every cap. Reinstalling *without*
it (a new save, or a co-save that was deleted) puts every cap back to 100 while
those skills keep their higher levels; they simply sit above their cap and stop
earning XP until the cap is raised past them again. No resource you already
spent is refunded.

## License

GPL-3.0-or-later (CommonLibSSE-NG is GPL-3.0-or-later and statically linked).
