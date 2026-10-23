# Beyond Mastery

SKSE plugin for Skyrim AE 1.6.x and 1.7.x: raise each skill's cap above 100 in +10 steps, paid with perk points, dragon souls and filled black soul gems. Skill effects above 100 continue sub-linearly or asymptotically.

- Requirements: [PRD.md](PRD.md)
- Engine hook design: [docs/hook-points.md](docs/hook-points.md)

## Status

Supported runtimes: AE 1.6.x and 1.7.x. SE 1.5.97 is out of scope for now
(PRD §3): the plugin loads there but stays inactive.

Verified in-game on 1.7.104 (SKSE64 2.3.1, Proton):
- The plugin loads, with no warnings in its log.
- Every patch site resolves to the address `scripts/verify_sites.py` predicts
  ([docs/hook-points.md](docs/hook-points.md) §9a).
- Skills level normally and stop at their limit.
- The Legendary menu: Upgrade pays with perks, dragon souls and black soul
  gems (The Black Star included). It shows what is missing when the player
  can't afford it, and names the level still to reach below the limit.
- Reset Skill, re-training, and dragon-soul gates paid again on each climb.
- Caps and gates survive save and reload.

Implemented, but not yet verified in-game:
- The reworded Reset Skill confirmation (FR-31).
- The first level past a raised limit costs XP (FR-8).
- The conflict warning (FR-20).

Not yet implemented: formula scaling above 100, FR-10's double XP accumulator
and FR-11's character-XP clamp.

## Install

Copy `BeyondMastery.dll` to `Data/SKSE/Plugins/`. Requires SKSE64 and Address
Library for SKSE Plugins.

## Uninstalling

Skills already above 100 keep the level they reached — the engine stores skill
levels as floats, so nothing resets — but they stop progressing once the plugin
is gone, and nothing above 100 will rise again.

Reinstalling with the co-save intact restores every cap. Reinstalling *without*
it (a new save, or a co-save that was deleted) puts every cap back to 100 while
those skills keep their higher levels; they simply sit above their cap and stop
earning XP until the cap is raised past them again. No resource you already
spent is refunded.

## Development

Building, testing and the architecture: [docs/development.md](docs/development.md).

## License

GPL-3.0-or-later (CommonLibSSE-NG is GPL-3.0-or-later and statically linked).
