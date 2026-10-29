# Custom Skills Above 100 (Plan 1: core) Implementation Plan

> **Note:** The effective level (Task 6) and its INI keys `sFormula`, `fTarget` and `sEffectiveGlobal` were dropped on 2026-10-29 by the project owner's decision: a skill mod that needs scaling above 100 handles it in its own patch. The text below is kept as written.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let players opt individual Custom Skills Framework (CSF) skills into Beyond Mastery: caps above 100 through Legendary → Upgrade, a per-skill perk-point source, and a published effective level. Configured through `BeyondMastery.ini`.

**Architecture:** A game-free header holds the per-skill settings model, form references, the effective-level formulas and the co-save encoding, all unit-tested. A game-side registry reads CSF's JSON files and resolves their forms. The cap comes from Beyond Mastery's state through four byte-checked xbyak stubs in `CustomSkills.dll` 3.2.0. The Legendary menu, resources and state are generalised from `RE::ActorValue` to a skill handle that also covers custom skills.

**Tech Stack:** C++23 SKSE plugin (CommonLibSSE-NG, clang-cl cross-compile), xbyak, nlohmann-json and SimpleIni (both already in `vcpkg.json`), native C++20 tests run by `scripts/test.sh`.

**Spec:** `docs/superpowers/specs/2026-10-29-custom-skills-above-100-design.md`. The MCM (spec goal 2) is **Plan 2**, written after a tooling spike. Plan 1 must leave every setting reachable through one C++ API, so Plan 2 only adds Papyrus natives on top of it.

## Global Constraints

- Branch: `feat/customskills-compat` (PR #14). A change to something PR #14's commit `6a7ef60` introduced (`src/Compat.*`, the slot check in `src/Hooks.cpp`, the conflict-list migration) is folded into that commit with `git commit --fixup=6a7ef60` and `GIT_SEQUENCE_EDITOR=: git rebase -i --autosquash origin/master`. Everything else goes in logical commits. Push with `--force-with-lease`.
- Commit titles follow `.gitlint` (Conventional Commits). Every commit ends with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Game-free headers (`src/CustomSkillsData.h`, new) must not include `RE/`, `SKSE/` or `PCH.h`, and must compile as C++20 in `scripts/test.sh` (`docs/development.md`, "Rules for code").
- The CSF patch covers CSF **3.2.0** only, identified by byte patterns. On any mismatch nothing in CSF is written, enabled custom skills stay at 100, and the log says why (spec, anti-goals).
- With no custom skill enabled, no CSF patch is installed.
- No CSF code is copied. Facts about its binary are used, as for the engine.
- INI section per skill: `[CustomSkill.<id>]` with keys `bEnable` (default `false`), `sPerkSource` (`player`|`own`, default `player`), `sFormula` (`none`|`sqrt`|`asymptotic`, default `none`), `fTarget` (default `0`) and `sEffectiveGlobal` (`plugin|hexFormID`, default empty). Sections of uninstalled skills are kept.
- Upgrade costs for custom skills are the vanilla ones (`Curves::CostOfNextStep`, `PlanGems`, the gates).
- `bScaleXPGainAbove100` applies to enabled custom skills, for skill use only (CSF's `a_isSkillUse == true`), as FR-9 does for vanilla skills.
- FR-8 isn't carried over: Upgrade on a custom skill leaves CSF's `Ratio` alone.

## Review Focus

1. **A CSF build other than 3.2.0, or another plugin's patch in those functions:** nothing is patched, no crash, one log line per site, and enabled skills stay at 100. Covered by Task 4's pattern test.
2. **`sFormula = asymptotic` with `fTarget ≤ 100`, or a non-number:** it behaves as `none` and is logged, with no NaN or inverted curve. Covered by Task 1's test.
3. **`sPerkSource = own` for a skill whose JSON has no `perkPoints`:** it falls back to `player` and is logged, so Upgrade isn't blocked forever. Covered by Task 1's test.
4. **A save whose co-save holds a custom skill that is no longer installed:** the entry survives a save/load round trip unchanged. Covered by Task 3's test.
5. **Two installed custom skills with the same display name:** the Legendary lookup by name is ambiguous, so the vanilla box opens and it's logged. Covered by Task 2's test.

---

### Task 1: Game-free settings model and formulas

**Files:**
- Create: `src/CustomSkillsData.h`
- Test: `tests/test_customskills.cpp` (add `test_customskills` to the loop in `scripts/test.sh`)

**Interfaces:**
- Produces:
  - `namespace BM::CustomSkillsData`
  - `struct FormRef { std::string plugin; std::uint32_t localID; }`
  - `std::optional<FormRef> ParseFormRef(std::string_view)`: CSF's `"plugin.esp|D63"` syntax, hex without prefix
  - `enum class PerkSource { kPlayer, kOwn }`
  - `enum class Formula { kNone, kSqrt, kAsymptotic }`
  - `struct Settings { bool enable{false}; PerkSource perkSource{PerkSource::kPlayer}; Formula formula{Formula::kNone}; double target{0.0}; std::optional<FormRef> effectiveGlobal; }`
  - `struct RawSettings { std::string enable, perkSource, formula, target, effectiveGlobal; }`: strings as read from the INI
  - `struct Parsed { Settings settings; std::vector<std::string> warnings; }`
  - `Parsed ParseSettings(const RawSettings&, bool a_hasOwnPool)`
  - `std::string SectionName(std::string_view a_id)`: returns `"CustomSkill." + id`
  - `double EffectiveLevel(Formula, double a_target, double a_level)`

- [ ] **Step 1: Write the failing tests** in `tests/test_customskills.cpp`, in `tests/test_curves.cpp`'s style (own `CHECK` macros, a file-header comment saying what it covers):
  - `ParseFormRef("myskilltest.esp|D63")` gives plugin `myskilltest.esp`, localID `0xD63`. `"x.esp|"`, `"|D63"`, `"x.esp|ZZ"` and `""` give `nullopt`.
  - `ParseSettings({"true","own","asymptotic","150","x.esp|1"}, true)` gives every field set and no warnings.
  - `ParseSettings({"", "own", "", "", ""}, false)` gives `perkSource == kPlayer` and one warning containing `"own"`.
  - `ParseSettings({"", "", "asymptotic", "100", ""}, true)` and `(…, "abc", …)` give `formula == kNone` and one warning each.
  - Unknown `sFormula`/`sPerkSource` values give the defaults plus a warning.
  - `SectionName("lolligagging") == "CustomSkill.lolligagging"`.
  - `EffectiveLevel(kNone, 0, 250) == 250`. `EffectiveLevel(kSqrt, 0, 400) == SqrtEffectiveSkill(400)` (== 300). For any formula, `EffectiveLevel(f, 150, 80) == 80`.
  - `EffectiveLevel(kAsymptotic, 150, 100) == 100`, it's strictly increasing on [100, 1000], stays `< 150` at 1000, and its slope at 100⁺ is ≈ 1 (within 1e-3).
- [ ] **Step 2: Run `scripts/test.sh`.** Expected: compile failure, because `CustomSkillsData.h` doesn't exist.
- [ ] **Step 3: Implement `src/CustomSkillsData.h`.** `kAsymptotic` is `Curves::Asymptote{ .limit = target, .valueAt100 = 100, .slopeAt100 = 1 }`, and `kSqrt` is `Curves::SqrtEffectiveSkill`. Both apply only above `Curves::kVanillaCap`. Booleans accept the same spellings SimpleIni's `GetBoolValue` does (`true/false/1/0/yes/no/on/off`).
- [ ] **Step 4: Run `scripts/test.sh`.** Expected: all tests pass, including `test_customskills`.
- [ ] **Step 5: Commit** `feat: model per-skill settings for custom skills`.

### Task 2: Registry: CSF JSON files, forms, INI sections

**Files:**
- Create: `src/CustomSkills.h`, `src/CustomSkills.cpp` (add both to `CMakeLists.txt`'s source list)
- Modify: `src/Config.h`, `src/Config.cpp` (read and write the per-skill sections), `src/main.cpp` (resolve at kDataLoaded, before `Hooks::InstallAll`)
- Modify (fixup into `6a7ef60`): `src/Compat.h`, `src/Compat.cpp`, to expose `CustomSkillsLoaded()`
- Test: name-ambiguity logic as a game-free function in `src/CustomSkillsData.h`, tested in `tests/test_customskills.cpp`

**Interfaces:**
- Consumes: Task 1.
- Produces:
  - `namespace BM::CustomSkills`
  - `struct Skill { std::string id, name, file; CustomSkillsData::FormRef level; std::optional<CustomSkillsData::FormRef> legendary, perkPoints; RE::TESGlobal* levelGlobal{}; RE::TESGlobal* legendaryGlobal{}; RE::TESGlobal* perkPointsGlobal{}; RE::TESGlobal* effectiveGlobal{}; CustomSkillsData::Settings settings; }`
  - `void Discover()`: parses `Data/SKSE/Plugins/CustomSkills/*.json` with nlohmann-json, called from `Config::Load`. `perkPoints` is the file-level field of the JSON (a group pool shared by the file's skills).
  - `void Resolve()`: looks up the `TESGlobal`s at kDataLoaded and logs each unresolved form
  - `const std::vector<Skill>& All()`
  - `Skill* FindByLevelGlobal(const RE::TESGlobal*)`
  - `Skill* FindByName(std::string_view)`: `nullptr` if the name is shared by more than one skill
  - `bool AnyEnabled()`
  - `bool SetSetting(std::string_view a_id, std::string_view a_key, std::string_view a_value)`: validates through `ParseSettings`, updates the skill and saves the INI. This is the single entry point Plan 2's MCM calls.
  - `CustomSkillsData::UniqueNames(const std::vector<std::string>&) -> std::set<std::string>`, game-free

- [ ] **Step 1: Write the failing test.** `UniqueNames({"A","B","A"}) == {"B"}`.
- [ ] **Step 2: Run `scripts/test.sh`.** Expected: compile failure.
- [ ] **Step 3: Implement** `UniqueNames`, the registry, the `Config` integration and `Compat::CustomSkillsLoaded()`:
  - **Sections:** one is written per discovered skill. Its comment line reads `; <name> (from CustomSkills/<file>)`, and the keys follow the Global Constraints. Sections without a discovered skill are left in the file untouched.
  - **Warnings:** the warnings from `ParseSettings` are logged with the skill id.
  - **CSF absent:** if `Compat::CustomSkillsLoaded()` is false, `Discover` still runs, so the INI sections exist, but `Resolve` logs once that CSF isn't loaded and enables nothing.
- [ ] **Step 4: Run `scripts/test.sh` and `scripts/build.sh`.** Expected: both pass.
- [ ] **Step 5: Commit** the fixup for the `Compat` part, then `feat: read custom skills and their settings`.

### Task 3: Custom skill caps in state and co-save

**Files:**
- Modify: `src/State.h`, `src/State.cpp`
- Modify: `src/CustomSkillsData.h` (record encoding)
- Test: `tests/test_customskills.cpp`

**Interfaces:**
- Consumes: Task 2's `Skill::id`.
- Produces:
  - `struct CustomCap { std::uint16_t cap{100}; std::uint8_t gatesPaid{0}; }`
  - `CustomSkillsData::EncodeCaps(const std::map<std::string, CustomCap>&) -> std::vector<std::byte>`
  - `CustomSkillsData::DecodeCaps(std::span<const std::byte>) -> std::optional<std::map<std::string, CustomCap>>`. The layout is a u32 count, then per entry a u16 id length, the id bytes, a u16 cap and a u8 gates. It rejects truncated input and caps that fail `Curves::IsPlausibleCap`.
  - `State::CustomCapOf(std::string_view id) const -> CustomCap` (default `{100, 0}`)
  - `State::SetCustomCap(std::string_view id, CustomCap)`
  - `State::CustomLimitOf(std::string_view id) const -> std::uint16_t` (`Curves::Limit`)
  - A co-save record `'CCAP'`, version 1, written only when the map is non-empty. Revert clears the map.

- [ ] **Step 1: Write the failing tests:**
  - An encode/decode round trip of `{"lolligagging": {110, 1}, "gone_mod_skill": {300, 3}}` returns the same map. The removed mod's entry survives (Review Focus 4).
  - A truncated buffer and a cap of 105 each give `nullopt`.
- [ ] **Step 2: Run `scripts/test.sh`.** Expected: compile failure.
- [ ] **Step 3: Implement** the encoding and the `State` members. In `OnLoad`, a `'CCAP'` record with an unknown version is logged and skipped, so the vanilla caps still load (FR-22).
- [ ] **Step 4: Run `scripts/test.sh` and `scripts/build.sh`.** Expected: both pass.
- [ ] **Step 5: Commit** `feat: keep custom skill caps in the co-save`.

### Task 4: Patch CSF 3.2.0's level limit

**Files:**
- Create: `src/CustomSkillsSites.h` (game-free; CSF 3.2.0 RVAs and patterns, in `HookSites.h`'s style)
- Modify: `src/Stubs.h` (two stubs and `kTrampolineSize`), `src/Hooks.cpp` (install), `src/CustomSkills.cpp` (cap and XP callbacks)
- Test: `tests/test_stubs.cpp`; `tests/test_customskills_sites.py`, which checks the patterns against a real `CustomSkills.dll` when `BM_CSF_DLL` points to one and skips otherwise, like `tests/test_verify_sites.py`

**Interfaces:**
- Consumes: `CustomSkills::FindByLevelGlobal`, `State::CustomLimitOf`, `Curves::XPGainMultiplier`, `Compat::CustomSkillsLoaded`.
- Produces:
  - `std::int32_t CustomSkills::LevelLimit(const RE::TESGlobal* a_level)`: the limit for an enabled skill, otherwise 100
  - `float CustomSkills::UseXPFactor(const RE::TESGlobal* a_level)`: `XPGainMultiplier(level)` for an enabled skill with `bScaleXPGainAbove100`, otherwise 1

The facts below were established from CSF 3.2.0's PDB and disassembly. The module is `CustomSkills.dll`, and offsets are relative to each function's RVA. Verify them again before writing.

| Function | RVA | Entry bytes | Site | Instruction | Live registers |
|---|---|---|---|---|---|
| `Skill::Advance` | `0x4CAE0` | `48 8B C4 48 89 58 18 55 56 57 41 56 41 57` | `+0x17B` | `cmp esi, 0x64` / `jge` → `+0x38B` | `this` = r14, level = esi, `Level` = `[r14+0x40]` |
| `Skill::Advance` | `0x4CAE0` | | `+0x382` | `cmp esi, 0x64` / `jl` → `+0x1B0` (loop back) | same |
| `Skill::Increment` | `0x4D1D0` | `85 D2 0F 84` | `+0x8C` | `cmp r14d, 0x64` / `jge` | `this` = rdi, level = r14d, `Level` = `[rdi+0x40]` |
| `Skill::Increment` | `0x4D1D0` | | `+0x96` | `mov eax, 0x64` / `sub eax, r14d` | same |

`TESGlobal::value` is at `+0x34`. `Skill::Ratio` is at `+0x48`.

- [ ] **Step 1: Write the failing tests:**
  - `test_stubs.cpp` gets one case per stub, asserting the emitted bytes:
    - **save:** volatile registers saved and restored;
    - **call:** a call to the given callee with `rcx` = `[this+0x40]`;
    - **compare:** `cmp` of the level register against the returned limit;
    - **exits:** both jump targets.
  - `test_customskills_sites.py` has one test per site, matching the pattern at RVA + offset in `BM_CSF_DLL`.
- [ ] **Step 2: Run `scripts/test.sh`.** Expected: `test_stubs` fails to compile, and the Python test skips without `BM_CSF_DLL`.
- [ ] **Step 3: Implement.**
  - **Locating the sites:** each site is found at RVA + offset in the module from `GetModuleHandleW(L"CustomSkills.dll")`, after the entry bytes match. Each site is replaced by a `write_branch<5>` to its stub, and the stub ends by jumping to whichever original target applies.
  - **The XP factor:** it scales the incoming use XP inside `Advance` before the loop. The builder chooses the site from the disassembly, after the `HandleEntryPoint(kModSkillUse)` call and only when `a_isSkillUse` is true, and adds it to the table and the tests in the same way.
  - **All or nothing:** either every site matches or none is written.
  - **Install conditions:** install only if `Compat::CustomSkillsLoaded()` and `CustomSkills::AnyEnabled()` are both true. Log one line per site, as `InstallAll` does.
- [ ] **Step 4: Verify.**
  - Run `scripts/test.sh`. Expected: all tests pass.
  - Run `BM_CSF_DLL=<scratch>/csf/SKSE/Plugins/CustomSkills.dll python3 -m unittest tests.test_customskills_sites`. Expected: every site test passes.
  - Run `scripts/build.sh`. Expected: OK.
- [ ] **Step 5: Commit** `feat: lift Custom Skills Framework's level limit for enabled skills`.

### Task 5: Legendary, Upgrade and Reset for custom skills

**Files:**
- Modify: `src/Menu.h`, `src/Menu.cpp`, `src/Resources.h`, `src/Resources.cpp`, `src/Texts.h` (only if a string needs the skill handle)
- Test: `tests/test_texts.cpp`, if `Texts.h` changes

**Interfaces:**
- Consumes: Tasks 2 and 3.
- Produces:
  - `using SkillRef = std::variant<RE::ActorValue, CustomSkills::Skill*>`, replacing `RE::ActorValue` in `Menu.cpp`'s internal functions (`ShowMain`, `ShowUpgrade`, `Confirm`, `Apply`, `ResetSkill`, `SkillName`, `BaseLevel`)
  - `Resources::Spend(const Curves::StepCost&, const Curves::GemPlan&, RE::TESGlobal* a_perkPool)`: `nullptr` means the player's perk points. `Resources::Read(RE::TESGlobal* a_perkPool)` likewise.

- [ ] **Step 1: Change** `OpenLegendary`.
  - **Resolving a custom skill:** for a callback whose `skill` is `'CSF' << 8` or above, resolve the skill with `CustomSkills::FindByName(RE::ActorValueList::GetActorValueInfo(skill)->GetFullName())`. CSF's `GetActorValueInfo` hook answers for its own values.
  - **Which box:** Beyond Mastery's box opens only for an enabled skill with a Legendary global. Anything else, including an ambiguous name (Review Focus 5), gets the vanilla box, and that's logged.
- [ ] **Step 2: Generalise** the menu functions to `SkillRef`:
  - **Upgrade:** raises `State::SetCustomCap`. It doesn't touch CSF's `Ratio`.
  - **Reset Skill:** runs CSF's flow through the vanilla callback. The custom skill's gates are cleared when its level global is next seen below 100, mirroring FR-31.
  - **Perk source:** the perk pool is the skill's `perkPointsGlobal` when `sPerkSource == own`, otherwise `nullptr`.
- [ ] **Step 3: Run `scripts/test.sh` and `scripts/build.sh`.** Expected: both pass.
- [ ] **Step 4: Commit** `feat: offer Upgrade for enabled custom skills`.

### Task 6: Effective-level global

**Files:**
- Modify: `src/CustomSkills.cpp`, `src/main.cpp`

**Interfaces:**
- Consumes: Task 1's `EffectiveLevel`, Task 2's `Skill::effectiveGlobal`.
- Produces: `void CustomSkills::PublishEffectiveLevels()`, which writes `EffectiveLevel(formula, target, levelGlobal->value)` into each enabled skill's `effectiveGlobal`.

- [ ] **Step 1: Implement.** It's called on game load (`State`'s load callback), after Upgrade, and on CSF's `SkillIncreaseEvent`. That event is reached through CSF's SKSE interface (`include/CustomSkills/Interfaces.h` in CSF's repository), requested at kPostLoad. A `sEffectiveGlobal` that doesn't resolve to a `TESGlobal` is logged once and skipped.
- [ ] **Step 2: Run `scripts/build.sh`.** Expected: OK.
- [ ] **Step 3: Commit** `feat: publish custom skills' effective level`.

### Task 7: Requirements, docs and the upstream draft

**Files:**
- Modify: `PRD.md` (v1.14: FR-34 per-skill opt-in and settings, FR-35 CSF patch and fallback, FR-36 effective level; §8 open criteria for them), `docs/nexus/description.md`, `docs/hook-points.md` (a CSF 3.2.0 section with the method behind Task 4's table), `docs/development.md` (map rows for `CustomSkills.*`, `CustomSkillsData.h`, `CustomSkillsSites.h`; `CustomSkillsData.h` and `CustomSkillsSites.h` join the game-free list)
- Create: the upstream proposal as `<scratchpad>/csf-max-level-provider.md`, not in the repository. It proposes `SetMaxLevelProvider(std::uint32_t (*)(const char* skillId))` with an interface version bump, and explains the use case and the fallback to 100.

- [ ] **Step 1: Write the docs.** Each fact lives in one place, with links elsewhere (global CLAUDE.md, "Single Source of Truth").
- [ ] **Step 2: Check.**
  - Run `scripts/test.sh` and `scripts/format.sh --check`. Expected: both pass.
  - Check every relative link resolves.
- [ ] **Step 3: Commit** `docs: specify custom skills above 100`, push with `--force-with-lease`, and update PR #14's description and issue #12's checklist.
- [ ] **Step 4: Hand the user** the upstream draft and the in-game checklist from the spec's "Verification" section.
