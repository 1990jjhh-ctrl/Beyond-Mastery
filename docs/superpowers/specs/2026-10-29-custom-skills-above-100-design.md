# Custom skills above 100 — design

Status: draft for review. Builds on PR #14 (Custom Skills Framework support, FR-33), which must be merged first.

> **Note:** The effective level (goal 4) and its INI keys `sFormula`, `fTarget` and `sEffectiveGlobal` were dropped on 2026-10-29 by the project owner's decision: a skill mod that needs scaling above 100 handles it in its own patch. The text below is kept as written.

## Intent

Custom Skills Framework (CSF) lets other mods add skill trees, such as an Unarmed skill. Today Beyond Mastery leaves those skills entirely to CSF, which stops them at 100. Players should be able to opt individual custom skills into Beyond Mastery, so they can be raised past 100 the same way, and at the same price, as vanilla skills.

CSF's release pace is unknown, so the fast route is to patch CSF's binary. At the same time, CSF gets an upstream proposal for a proper callback. If the callback ships, Beyond Mastery switches to it and the patch becomes a fallback for older CSF versions.

## Goals

1. **Detection and settings.** Beyond Mastery lists every CSF skill at startup and keeps four settings per skill in `BeyondMastery.ini`, in a section `[CustomSkill.<Id>]` (`<Id>` is the skill's `id` from its CSF JSON file):
   - `bEnable` (default `false`): Beyond Mastery handles this skill above 100.
   - `sPerkSource` (`player` or `own`, default `player`): where Upgrade's perk point comes from. `own` is offered only if the skill's JSON defines `perkPoints`. A limited own pool would otherwise block uncapping, so the player picks.
   - `sFormula` (`none`, `sqrt` or `asymptotic`, default `none`) and `fTarget` (default 0): how the skill's *effective level* is computed above 100. `sqrt` is form A (FR-24), with `fTarget` unused. `asymptotic` heads for `fTarget` as the skill grows, in the shape of form B (FR-25).
   - `sEffectiveGlobal` (`plugin|formID`, default empty): the global Beyond Mastery writes the effective level into.
2. **MCM.** A SkyUI MCM page "Beyond Mastery" with one section per installed custom skill, editing exactly those settings. Changes take effect immediately and are written to the INI. The formula fields say plainly that they only matter for a skill mod, or a patch for one, that reads the named global instead of its raw level.
3. **Enabled skills get the vanilla treatment above 100:**
   - Legendary at the skill's cap opens Beyond Mastery's Upgrade / Reset Skill / Cancel box instead of vanilla's.
   - Upgrade raises the cap by +10 at the same costs as vanilla skills (FR-5 … FR-7): perk points, black soul gems from cap 200, and dragon souls at each hundred. Gates recur after a reset (FR-13). The perk point comes from the source chosen in `sPerkSource`.
   - Reset Skill is CSF's own Legendary reset, wrapped the same way as vanilla's (FR-31).
   - Caps and paid gates persist in Beyond Mastery's co-save under the skill's `id` (FR-22).
   - `bScaleXPGainAbove100` (FR-9) applies to enabled custom skills too.
4. **Effective level.** For an enabled skill with a formula and a global, Beyond Mastery writes the effective level into that global whenever the skill's level changes or the game loads. At 100 and below it equals the level. If the global is missing (plugin not loaded, wrong form ID, or not a global), Beyond Mastery logs it and writes nothing.
5. **Disabled skills** behave exactly as with PR #14, i.e. purely CSF's.
6. **Upstream.** A drafted issue/PR text for Exit-9B/CustomSkills that proposes a max-level provider in CSF's SKSE interface. The user posts it.

## Non-goals

- **Scaling a custom skill's effects on its own.** Custom skills implement their effects in perks and scripts that read the level global directly, and Beyond Mastery can't see those reads. It only publishes the effective level (goal 4); using it is up to the skill mod or a patch for it. Writing such patches for specific skill mods isn't part of this work.
- **FR-8 ("first level after a breakthrough is not free")** isn't carried over. CSF stops adding progress once a skill is at its cap, so there's no stored progress for a free level. The builder verifies this in-game; if it doesn't hold, it becomes a finding.
- **The character-XP clamp (FR-11)** for custom skills. CSF's own `enableXPPerRank` governs that.
- **Custom skills without a Legendary global**, i.e. a JSON with no `legendary`. CSF offers no Legendary for them, so there's nothing to hook. Their switch still appears, but enabling one only logs that it has no effect.

## Anti-goals

- A CSF update must never crash or corrupt a save. If any patch site doesn't match, nothing in CSF is patched, enabled custom skills stay at 100, and the log says why. That's the same "check the bytes before writing" rule as for the engine (`docs/development.md`, "Rules for code").
- An MCM setting that silently does nothing. The formula fields carry the note from goal 2, and a missing global is logged.
- Nothing changes for a player who has enabled no custom skill. With every switch false, no CSF patch is installed (the "don't install a no-op hook" rule).
- No CSF code is copied into Beyond Mastery. CSF is MIT-licensed, but nothing needs copying: facts about its binary are used, as for the engine.

## Constraints

- Supported CSF version for the patch: **3.2.0**, verified by byte patterns. The version comes from `SKSE::PluginInfo` (`CustomSkills`, 3.2.0) and the byte checks decide on their own.
- The four CSF sites (from CSF 3.2.0's PDB and disassembly, file `SKSE/Plugins/CustomSkills.dll`, `.text` RVAs relative to the module):
  - `Skill::Advance`: two `cmp esi, 0x64` loop conditions (`level < MaxLevel()`).
  - `Skill::Increment`: `cmp r14d, 0x64` (`level >= MaxLevel()`) and `mov eax, 0x64` (`MaxLevel() - level`).
  - Trainers reach `Increment` through CSF's own hook, so they're covered by it.
  - Skill books: the builder confirms whether they go through `Advance` or `Increment`. CSF's `SkillBooks` hook suggests `Advance`.
- Each site is located inside its function by pattern search within the function's extent, not by a fixed offset, because CSF has no Address Library. The function addresses come from a per-version table of RVAs, verified by a byte pattern at each function's entry.
- The per-skill cap must come from Beyond Mastery's state for that skill. At the patch sites, the CSF `Skill*` is in a register. Beyond Mastery identifies the skill by its **Level global's form ID** (the `Skill::Level` member), not by reading CSF's `std::string ID`, to depend on as little of CSF's struct layout as possible. It maps form ID to `id` by parsing the same JSON files CSF reads (`Data/SKSE/Plugins/CustomSkills/*.json`, field `skills[].level`).
- **Plugin file.** The MCM needs a quest, so Beyond Mastery ships its first plugin file: `BeyondMastery.esp`, ESL-flagged (no load-order slot), holding only the MCM quest and its player alias. Requirements grow by SkyUI, but only for the menu. Without SkyUI the plugin still loads and the INI still works, and the README and Nexus page say so.
- **Papyrus stays glue.** The MCM script only lays out the page and calls native functions that the DLL registers (`BeyondMastery.GetCustomSkills()`, `GetSetting…`/`SetSetting…`). All logic, validation and INI writing stay in C++.
- **Build.** The `.pex` and `.esp` must be produced by `scripts/build.sh` on Linux, or checked in with a script that regenerates them. The builder picks a Papyrus compiler that runs there (e.g. Caprica) and gets SkyUI's `SKI_ConfigBase` headers from SkyUI's SDK, checking their license (the project is GPL-3.0-or-later). The ESP is generated by a script, not edited by hand.
- Game-free logic (JSON-to-map parsing, INI key naming, cap lookup, the effective-level formulas) goes in game-free headers with tests (`docs/development.md`, "Rules for code").

## Decided how

- **Per-skill INI sections**, written back like every other setting, with a comment line naming the skill's display name and its source JSON. Sections are generated at load, so a newly installed skill mod gets its section on the next start. A section for a skill that's no longer installed is kept.
- **The MCM writes through the DLL:** `SetSetting…` updates `Config` and saves the INI at once, so the INI stays the single store and the MCM holds no state of its own.
- **The formulas reuse `Curves.h`:** `sqrt` is `SqrtEffectiveSkill`, and `asymptotic` is form B's decay with `fTarget` as its limit, applied to the level rather than a cost factor. They're exposed as level-to-effective-level functions with tests.
- **Patch shape:** each `cmp`/`mov` with the immediate 100 is redirected through a small stub that calls `Compat::CustomSkillCap(levelGlobal)`. That returns Beyond Mastery's cap for an enabled skill and 100 otherwise. The stubs are xbyak, in the existing `Stubs.h` style, and sized into `kTrampolineSize`.
- **Legendary:** CSF builds vanilla's `LegendarySkillResetConfirmCallback` with `skill = 'CSF' << 8 + index`. `Menu::OpenLegendary` resolves that index to the CSF skill (through the open StatsMenu's selected tree, as CSF itself does) and shows Beyond Mastery's box for enabled skills. Disabled or unknown skills keep the vanilla box (current behaviour).
- **Upgrade restarting progress:** for custom skills, Upgrade only raises the cap. CSF's `Ratio` is left alone, per the FR-8 non-goal.
- **Co-save:** a new record type holding `(id string, cap, gates)` for custom skills, versioned like the existing caps record (FR-22, `src/State.cpp`). Unknown ids, from a removed skill mod, are kept and written back unchanged, so reinstalling the skill mod restores them.
- **Upstream proposal:** the interface gains `SetMaxLevelProvider(std::uint32_t (*)(const char* skillId))`, plus a version bump of `CustomSkillsInterface`. Beyond Mastery uses it when present and skips the binary patch. The draft goes to the user as a markdown text, not posted.

## Left to the builder

- Exact patterns and stub layouts for the four sites, and the RVA table for 3.2.0.
- Where `XPGainMultiplier` (FR-9) is applied in `Advance`: it scales the incoming XP before the loop, mirroring the vanilla XPGainStub.
- How `Menu`'s cost and text code takes a custom skill. Today it takes `RE::ActorValue`, so the builder generalises it to a skill handle that covers both vanilla and custom skills.
- MCM page layout, option types (toggle, menu, slider/input) and the slider range for `fTarget`.
- INI section naming for ids (`lolligagging` → `bEnableBeyondMasteryForLolligagging`).
- The offset of `Skill::Level` inside CSF's `Skill` object, read from `Advance`'s disassembly. Byte patterns can't verify it, so the builder confirms it in-game: the patched cap lookup must hit the right skill.
- When the JSON is parsed. The INI switches are written in `SKSEPlugin_Load` (`Config::Load`), so the parse happens there too, independent of CSF's own parse at kDataLoaded.

## Verification

- **Unit tests:** JSON parsing into the form-ID-to-id map, key naming, the cap lookup (enabled vs. disabled vs. unknown), and the co-save record round trip including unknown ids.
- **In-game, with the CSF example skill (Lolligagging):**
  - Disabled: identical to PR #14.
  - Enabled: Legendary at 100 opens Upgrade; after Upgrade the skill levels to 110 and stops there.
  - Reset Skill keeps the cap.
  - The cap survives save/load.
  - With `bScaleXPGainAbove100`, XP gain is slower above 100.
  - MCM: one section per installed custom skill; toggling and choices persist in the INI and survive a restart.
  - `sPerkSource = own` spends the skill's own pool; `player` spends vanilla perk points.
  - With a formula and a global set, the global (checked with `GetGlobalValue` in the console) follows the effective level above 100 and equals the level below it.
  - The log names every patched CSF site.
- **Mismatch path:** load with a modified or other CSF version (or a forced pattern miss). Nothing is patched, the log explains, and the skill stays at 100.
