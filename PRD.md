# Beyond Mastery — Product Requirements Document

Status: **v1.13 — Approved** (2026-10-29; v1.12 adds Experience compatibility, FR-32; v1.13 Custom Skills Framework, FR-33)
Date: 2026-10-29
Owner: Joel Hirsbrunner

---

## 1. Problem

In Skyrim (SE/AE), skills stop leveling at 100. Once a skill is maxed, further use has no progression value, and the only vanilla continuation (Legendary) resets the skill to 15. Existing uncapper mods remove or raise the cap globally via configuration, without in-game cost or progression.

## 2. Goal

Let the player raise each skill's cap beyond 100 in +10 steps by paying an escalating in-game price (perk points, dragon souls, filled black soul gems). Levels above 100 are still earned through normal skill use. Progression becomes progressively harder until it is effectively impossible without external XP boosts (soft cap, no hard limit).

Skill effects above 100 must keep rewarding the player but never run away: every skill-dependent formula grows sub-linearly or approaches a fixed limit (§6.9).

## 3. Non-goals

- No XP boosts, training changes, or new perks/perk trees.
- No changes to formulas at skill ≤ 100. Vanilla behavior is untouched up to 100.
- No console / Bethesda.net Creation version (requires SKSE).
- No LE (Oldrim) support.
- No SE 1.5.97 support for now. Its patch sites could not be verified without a 1.5.97 binary, and a guessed patch risks corrupting XP or crashing. On 1.5.97 the plugin loads, changes nothing (vanilla behavior: skills stop at 100, Legendary unchanged) and says so in its log. To be revisited once a 1.5.97 install is available.
- No MCM requirement.

## 4. Target users & platform

- PC players on Skyrim **AE 1.6.x** and **1.7.x**, using SKSE64 and Address Library. SE 1.5.97 is out of scope (§3).
- A single DLL via **CommonLibSSE-NG**. No plugin file (ESP/ESL): the entry point is vanilla Legendary (FR-14).

## 5. Definitions

| Term | Meaning |
|---|---|
| Cap | The highest level a skill has been upgraded to. Default 100. Raised in +10 steps; survives Reset Skill. |
| Gate *T* | The boundary at level `T × 100` (T ≥ 1). Passing it costs `10 × T` dragon souls, once per climb. |
| Climb | The span from one Reset Skill of a skill to the next (or from the start of the game to the first). |
| Limit | The level a skill can currently reach through XP: `min(cap, 100 × (1 + gates paid this climb))`. Equal to the cap until a Reset Skill clears the paid gates. |
| Step *n* | The *n*-th cap increase of a skill, counted continuously: step 1 = 100→110, step 10 = 190→200, step 11 = 200→210. |
| Tier *T* | `T = floor(currentCap / 100)`. Cap 100–190 = T1, 200–290 = T2, … |
| Filled black soul gem | A Black Soul Gem or The Black Star containing a black (humanoid) soul. |

## 6. Functional requirements

### 6.1 Cap raising

- **FR-1** Each of the 18 skills has an independent cap, default 100, stored persistently in the save (SKSE co-save).
- **FR-2** Upgrade is available only when the skill's base level **equals** its limit. It raises the limit by one of two means:
  - limit < cap — an unpaid gate is binding: Upgrade pays that gate. The cap is unchanged.
  - limit = cap — Upgrade raises the cap (FR-3), and also pays the gate when the raise crosses a 100-level boundary.
- **FR-3** Each raise increases the cap by exactly 10.
- **FR-4** No upper limit on the cap (soft cap through cost and XP curve).

### 6.2 Costs

Two kinds of cost. A **step** raises the cap from *C* to *C*+10, with
`T = floor(C / 100)`; the step number `n` identifies a step but is no longer an
input to its cost. A **gate** is paid to pass level `T × 100`, once per climb.

| Resource | Cost | When |
|---|---|---|
| Perk points | `1` | every step |
| Filled black soul gems | `T − 1` | every step |
| Dragon souls | `10 × T` | each gate *T*, once per climb (§5) |

An Upgrade that raises the cap across a boundary (`C = T × 100`) in a climb where
gate *T* is still unpaid costs both: the step and the gate.

- **FR-5** All costs of a step are checked before anything is deducted. If any resource is missing, nothing is consumed and the menu shows what is missing.
- **FR-6** Black soul gem consumption: consumable filled Black Soul Gems are removed from the inventory. The Black Star counts as one gem; its soul is emptied, the item is kept.
- **FR-7** If the player has more filled black soul gems than needed (consumable gems and/or The Black Star), the menu lets the player choose which ones to use.

Cost examples, first climb (one skill, never Reset):

| Step | Cap | Perks | Gems | Dragon souls |
|---|---|---|---|---|
| 1 | 100→110 | 1 | 0 | 10 |
| 2 | 110→120 | 1 | 0 | 0 |
| 10 | 190→200 | 1 | 0 | 0 |
| **Tier 1 total** | 100→200 | **10** | **0** | **10** |
| 11 | 200→210 | 1 | 1 | 20 |
| 20 | 290→300 | 1 | 1 | 0 |
| **Tier 2 total** | 200→300 | **10** | **10** | **20** |
| 21 | 300→310 | 1 | 2 | 30 |
| **Tier 3 total** | 300→400 | **10** | **20** | **30** |

Re-climb after Reset Skill, cap 250: the skill retrains from 15 and stops at
100 and again at 200 until each gate is paid — 10 + 20 = 30 dragon souls, and
nothing else, since the cap itself is already bought.

Rationale: an escalating perk cost made grind the binding constraint and put the
cost beyond what `PlayerCharacter::perkCount` can even hold (a `uint8`) past step
255. At a flat 1 perk per step the limiting factors are character level
progression and the two tier-scaled resources above. Dragon-soul gates recur
because Reset Skill on a raised cap is otherwise a free loop: drop to 15,
retrain past 100, collect character XP all the way. Recurring gates make each
loop cost something, and give dragon souls a lasting use once a character's
shouts are unlocked; dragons respawn, so the cost never locks a player out.

### 6.3 Leveling above 100

- **FR-8** Above 100, skills gain levels through normal XP from use, up to their individual cap. Raising a limit never grants a level: after an Upgrade the next level needs its full vanilla XP (FR-9), starting from zero progress.
- **FR-9** XP needed per level follows the vanilla formula (`ImproveMult × level^fSkillUseCurve + ImproveOffset`), extended past 100. Optionally (`bScaleXPGainAbove100`, default **off** = vanilla), the skill XP *gained from using* a skill at base level s > 100 is multiplied by `√(100/s)`: the slope of form A, so a level earns XP at the rate it adds effect. The factor is 1 at 100 and continuous (0.82 at 150, 0.71 at 200, 0.5 at 400). Whole-level grants (trainers, skill books) stay exact: they give precisely the XP missing to the next level.
- **FR-10** XP accumulation must remain correct at large thresholds. Below a threshold of 2²⁴ (≈ 1.68 × 10⁷, around level 3,500) float accumulation is exact; above it the plugin logs a warning once per skill and session. A `double` accumulator is deferred until a reachable level needs it.
- **FR-11** Character XP per skill level-up above 100 scales as in vanilla (`fXPPerSkillRank × new skill level`), not clamped. Optional clamp to the level-100 value via INI (`bClampCharXPAbove100`, default off).

### 6.4 Reset Skill (vanilla Legendary)

- **FR-12** Reset Skill does **not** reset the cap. The skill level resets to 15 as in vanilla and can be re-trained up to the cap, paying each gate again (FR-13).
- **FR-13** Reset Skill clears the gates paid during the climb. On the next climb each gate below the cap must be paid again (`10 × T` dragon souls); perks and gems already spent on the cap are not charged again.

### 6.5 Interaction / UI

- **FR-14** The entry point is vanilla Legendary. The skill tree's **[Legendary]** prompt is unchanged; pressing it on a skill at level 100 or above opens the Beyond Mastery menu in place of vanilla's confirmation box.
- **FR-15** The menu offers **Reset Skill**, **Upgrade** and **Cancel** for the selected skill. Upgrade is listed only when the skill's base level equals its limit (FR-2); below the limit the menu names the level still to reach and offers Reset Skill and Cancel.
- **FR-16** Selecting Upgrade shows the cost of that upgrade and the player's current resources (including the gem choice per FR-7), with **Confirm** and **Back**; confirming applies it. Costs are not shown before Upgrade is selected.
- **FR-17** Works without SkyUI/MCM. Menus use vanilla message boxes; the skill tree's own UI is not modified.
- **FR-31** Reset Skill is vanilla Legendary. Vanilla confirms twice; choosing Reset Skill stands in for the first box, and vanilla's own second box follows, so the flow keeps vanilla's two steps. The effect is vanilla's: level reset to `fLegendarySkillResetValue` (15), perk points returned. The second box's text is always replaced: *"<Skill> goes back to 15 and its perk points are returned. Its cap stays at N, but the M dragon souls spent on it are lost. Are you sure you want to reset this skill?"* M is what the current climb's gates cost; the clause is left out when no gate was paid, which is always the case at cap 100.

### 6.6 Configuration (INI)

- **FR-18** `BeyondMastery.ini` next to the DLL, at minimum:
  - `bScaleXPGainAbove100` (bool, default false)
  - `bClampCharXPAbove100` (bool, default false)
  - `bWarnOnConflict` (bool, default true)

### 6.7 Compatibility

- **FR-19** Standalone: the plugin installs its own cap and XP hooks.
- **FR-20** On load, detect known uncappers (e.g. SKSE Skill Uncapper). If found and `bWarnOnConflict` is true, show one in-game notification and write to the log.
- **FR-21** Write a log to the standard SKSE log directory (`BeyondMastery.log`).
- **FR-32** Experience is supported, not a conflict. Experience keeps character XP and its player-level skill cap; Beyond Mastery keeps Legendary, the paid caps and formula scaling. Both caps apply, and the lower one wins, below and above 100. While Experience is loaded, the character-XP clamp (FR-11) is not installed, because Experience owns character XP. An INI whose `sKnownConflicts` still holds an old default that listed Experience or Custom Skills Framework is updated to the new default.
- **FR-33** Custom Skills Framework is supported, not a conflict. Its custom skills stay entirely its own: it levels them itself, capped at 100, with its own Legendary. Beyond Mastery covers the 18 vanilla skills as without it. Formula scaling wraps CSF's GetActorValue, which forwards every vanilla skill to the engine's. Skyrim Uncapper stays a conflict (FR-20): it pursues the same goal by another route, so players choose one.

### 6.8 Save safety

- **FR-22** Caps persist via the SKSE co-save. Missing co-save data on load = all caps 100.
- **FR-23** Uninstalling: skills above 100 keep their current level (vanilla stores them as floats) but stop progressing. Documented in the readme.

### 6.9 Formula scaling above 100

Principle: for skill `s > 100`, each affected formula is continued with a curve that matches vanilla's **value and slope at 100** (C¹-continuous), then grows sub-linearly or approaches a limit. For `s ≤ 100` the formula is unchanged.

**A. Sub-linear (square root) — formulas linear in skill**

The formula is fed an effective skill instead of the real one:

`s_eff(s) = 100 + 200 · (√(s/100) − 1)` for s > 100, otherwise `s_eff = s`

s_eff(100) = 100 and s_eff'(100) = 1, so the transition is exact.

| s | 100 | 150 | 200 | 300 | 400 |
|---|---|---|---|---|---|
| s_eff | 100 | 145 | 183 | 246 | 300 |

**B. Asymptotic — formulas that would break**

General form: `f(s) = L + (f₁₀₀ − L) · e^(−(s−100)/τ)`, where `τ = (f₁₀₀ − L) / |f'₁₀₀|` matches vanilla's slope at 100.

| Formula | Vanilla | Limit L | f₁₀₀ | τ |
|---|---|---|---|---|
| Spell cost skill factor | `1 − (s/400)^0.65` | **0.25** | 0.594 | ≈ 130 |
| Barter skill price factor | `3.3 − 1.3·s/100` | see FR-26 | 2.0 | — |
| Enchanting charge-cost skill factor | `1 − (s·0.005)^0.5` | **f₁₀₀ / 2 ≈ 0.146** | 0.293 | ≈ 41 |

Spell cost skill factor, for comparison:

| s | 100 | 150 | 200 | 300 | 400 |
|---|---|---|---|---|---|
| vanilla | 0.594 | 0.47 | 0.363 | 0.18 | 0.0 |
| new | 0.594 | 0.48 | 0.41 | 0.33 | 0.29 |

**Requirements**

- **FR-24** Scope v1. Every formula below gets an explicit hook.
  - Sub-linear (A): weapon damage (One-Handed, Two-Handed, Archery), armor rating (Light, Heavy Armor), Smithing temper strength, Enchanting magnitude, Alchemy potion magnitude, Block, Lockpicking, Pickpocket, Sneak.
  - Asymptotic (B): spell cost (all six magic schools), barter, enchanting charge cost per use.
- **FR-25** Spell cost: the 0.25 limit applies to the **skill factor only**. Perk, equipment and dual-cast multipliers still apply on top, as in vanilla.
- **FR-26** Barter: the limit applies to the **final buy/sell ratio** after skill, perks and Fortify Barter. For the same item, buy price ≥ **1.2 ×** sell price. Above 100, the skill price factor follows form B with L chosen so that this ratio approaches 1.2. The final ratio is additionally clamped at ≥ 1.2.
- **FR-27** Existing vanilla clamps (e.g. armor damage-reduction cap 80%, pickpocket max chance, lockpicking sweet-spot limits) stay in place. Form A only feeds them a smaller effective skill.
- **FR-28** The limits are configurable in the INI (`fSpellCostLimit`, `fBarterMinRatio`, `fEnchantChargeLimitFraction`), and `bEnableFormulaScaling` switches all scaling off. The form per skill is fixed (form A, or form B for magic schools, Speech and enchanting charge). Defaults as above.
- **FR-29** The skill menu shows the real skill level, not s_eff.
- **FR-30** Enchanting charge cost per use: above 100, the skill factor follows form B with limit **half its value at skill 100** (vanilla ≈ 0.146). Vanilla's formula reaches zero cost at 200 and is undefined above; form B replaces it entirely for s > 100. The limit is configurable in the INI as a fraction of the value at 100 (default 0.5). Enchanting magnitude keeps form A.

  | s | 100 | 125 | 150 | 200 | 400 |
  |---|---|---|---|---|---|
  | vanilla | 0.293 | 0.209 | 0.134 | 0.0 | — |
  | new | 0.293 | 0.227 | 0.190 | 0.160 | 0.147 |

## 7. Constraints

- Requires SKSE64, Address Library for SKSE Plugins, and a CommonLibSSE-NG build covering AE 1.6.x and 1.7.x.
- Hook locations must be resolved through Address Library IDs (no hardcoded offsets). 1.7.x reuses the AE IDs: `REL::RelocationID` selects the AE ID for every runtime at or above 1.6.317, and the AE IDs, byte offsets and instruction patterns were confirmed unchanged on 1.7.104 (docs/hook-points.md, "Verification record").
- No plugin file: the entry point is vanilla Legendary (FR-14), so the mod adds no records and takes no load-order slot.

## 8. Success criteria

1. On the current AE version: pressing Legendary on a skill at 100 and choosing Upgrade raises its cap to 110 for 1 perk + 10 dragon souls; the skill then trains to 110 through use, then stops.
2. Step 11 (200→210) consumes exactly 1 perk point, 1 filled black soul gem, and 20 dragon souls; failure with any missing resource consumes nothing.
3. Caps survive save/load and Reset Skill. After Reset Skill, the retrained skill stops at each unpaid gate (100, 200, …) until Upgrade pays it.
4. Character XP from a skill-up at 150 is 1.5× that of a skill-up at 100 (clamp off), and equal to it with clamp on.
5. No crash or log error across 1 h of play with a typical 200+ plugin load order.
6. With another uncapper installed, the warning appears exactly once per session.
7. At skill 100, every hooked formula returns the same value as vanilla (±0.1%). At 101, the change matches vanilla's slope (±5%).
8. At Destruction 400, the spell cost skill factor is 0.29 ± 0.01, never below 0.25 at any skill.
9. At Speech 1000 with all barter perks and Fortify Barter, buy price ≥ 1.2 × sell price for any item.
10. At Enchanting 1000, a weapon enchantment still costs charge per hit (skill factor ≥ 0.146), and the item's number of uses is finite.
11. Reset Skill behaves as vanilla Legendary: level 15, perks returned, a second confirmation box shown. That box names the reset level, the returned perks and the kept cap, plus the dragon souls lost when any gate was paid.

**Results** (release 1.0.x on 1.7.104, SKSE64 2.3.1):

- **Verified in-game:** 1, 2, 3, 4, 6, 8, 9, 10 and 11. Criterion 4 was checked to the point with the plugin's `Character XP … (+N)` log line, which appears while the clamp is on. Criterion 6 was checked on a second machine, with Experience and Custom Skills Framework installed.
- **Unit-tested:** criterion 7, and the bounds in criteria 8 and 10 at every skill (`tests/test_curves.cpp`).
- **Open:** criterion 5 has not been run with a 200+ plugin load order. FR-10's one-time precision warning only fires around skill level 3,500 and has not been seen.

## 9. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| Perk cost is flat at 1 per step, so dragon souls and filled black soul gems are the only resource gates | Cap raises may be too cheap once souls and gems are stockpiled | Every gate costs 10 × T dragon souls once per climb, so each Reset Skill loop above 100 is paid again; every step past tier 1 costs T − 1 gems; character level-up cost grows steeply. Revisit after playtesting |
| Skill-dependent formulas are spread across many engine functions and perk entry points; no central "skill → effect" function | Each formula needs its own hook; missed one = vanilla linear scaling | Explicit formula list (FR-24); per-formula test at 100/101/200 |
| Some formulas may not read the skill value directly (e.g. via game settings or perk entry points) | Form A/B harder to inject | Identify per formula during reverse engineering; fall back to post-processing the result |
| Vanilla formulas on UESP may be inaccurate or unverified for some skills | Wrong f₁₀₀ / slope constants | Derive constants from the disassembly, not wiki values |
| Skill tree UI shows odd bars/values above 100 | Cosmetic | Accept; check in testing |
| Engine-side clamp lives in several code paths (XP gain, AV set, level-up) | Missed hook = cap not respected | Reverse-engineer and verify all paths; reference existing open-source uncappers |
| Future game updates move functions | Plugin breaks | Address Library IDs; version check on load |
| Conflict with other uncappers hooking the same functions | Double hooks, undefined behavior | Detect & warn (FR-20) |
| Other mods that change Legendary or its confirmation box | Their dialog and ours compete for the same press | Hook the confirmation box itself rather than the skill-tree UI; detect known Legendary mods at load and warn as in FR-20 |

## 10. Open points

None. Resolved in v0.4:

1. XP scaling above 100: gain factor `f^(T−1)`, f ≤ 1.0 (FR-9).
2. Gem choice: player chooses (FR-7).
3. ~~Dragon souls are kept on Legendary (FR-13).~~ Superseded in v1.4: gates recur per climb (FR-13).

Resolved in v1.4:

5. The menu is reached through vanilla Legendary, not a lesser power (FR-14 … FR-17, FR-31). No plugin file is needed. Costs appear only once Upgrade is selected.
6. Dragon-soul gates recur: Reset Skill clears them, and the next climb pays each one again (FR-13).

Resolved in v1.6:

7. SE 1.5.97 is out of scope until a 1.5.97 binary is available to verify its patch sites (§3).
4. Block, Lockpicking, Pickpocket and Sneak are linear in skill in vanilla → form A (FR-24). Sources: UESP Block, Pickpocket, Sneak, and the Lockpicking talk page (sweet-spot formula unofficial, to verify in disassembly).
