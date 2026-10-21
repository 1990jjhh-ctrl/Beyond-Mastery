# Beyond Mastery — Hook-Point Design

Status: Draft v0.1 (2026-10-21)
Scope: maps every PRD requirement that needs engine access to a concrete hook or API, for SE 1.5.97, AE 1.6.x and 1.7.x.

## Sources

- **Skyrim AE Uncapper (Rust)** by Kasplat et al., last commit 2026-09-07: <https://github.com/TheDreadedAndy/SkyrimAEUncapper-Rust>. Primary reference for Address Library IDs, offsets and byte signatures. The repo has no license file, so its code is **not** copied here. IDs, offsets and instruction patterns are used as facts about the game binary and must be re-verified in a disassembler.
- **CommonLibSSE-NG** (alandtse fork, GPL-3.0-or-later), last commit 2026-10-19: <https://github.com/alandtse/CommonLibSSE-NG>. Struct layouts (`PlayerCharacter::PlayerSkills`, `MessageBoxData`, `ActorValue`).
- UESP formula pages, see PRD §10.

## Conventions

- The machine-readable copy of every site below lives in `src/HookSites.h` (IDs, offsets, expected bytes, verified flag). That file is what the plugin uses; keep both in sync.

- `AE id+off` / `SE id+off`: Address Library ID plus byte offset into that function.
- Every patch site is guarded by a **byte-signature check** before writing. On mismatch the patch is skipped and logged, never written blind.
- Confidence: **H** = used in a maintained, released mod for both runtimes. **M** = reasoned from reference code, not yet seen in the binary. **L** = unknown, requires reverse engineering. Every site in §1–§5 has additionally been confirmed in our own disassembly of 1.7.104 (§9a), which is what sets `verified` in `src/HookSites.h`.

## 1. Skill cap (FR-1 … FR-4)

| What | SE | AE | Conf. |
|---|---|---|---|
| Per-skill cap read in the XP routine (replaces the constant 100.0 load) | 40554+0x45 | 41561+0x6F | H |

The hook returns the player's cap for the given skill from our co-save state. Signatures differ between SE and AE (AE has an extra `movaps`), so there are two wrappers.

Compatibility note: the reference mod overwrites ≥14 bytes here and NOPs the last two, as agreed with the eXPerience author (Nexus 17751). Same site = hard conflict with other uncappers → FR-20 detection.

## 2. Skill XP gain (FR-9, f^(T−1))

| What | SE | AE | Conf. |
|---|---|---|---|
| XP gain computed as `base × mult + offset` before it is added | 40554+0xDC | 41561+0xF1 | H |

The hook receives the skill and both XP components and returns the total. We multiply by `f^(T−1)` for base level ≥ 200 (T ≥ 2). At default f = 1.0 the hook is a no-op and can stay uninstalled.

**Known gap.** Just above this site the routine branches on its sixth argument
(`cmp BYTE PTR [rsp+0x1d8], 0; je`). The arm we patch computes
`ImproveMult × points + ImproveOffset`; the other arm stores the raw `points`
with no multiplier and is **not** hooked, so `f^(T−1)` does not apply to
whatever calls the engine with that flag clear. Reaching it needs a second
patch at that store (9 bytes, `movss [rsp+0x1b8], xmm6`). Unresolved: what the
flag means and which callers clear it. Until that is known the factor is a
reduction some XP sources escape, which under-applies the penalty rather than
over-applying it.

## 3. Character XP per skill-up (FR-11)

| What | SE | AE | Conf. |
|---|---|---|---|
| Character XP from a skill level-up, before `× fXPPerSkillRank` | 40576+0x70 | 41561+0x2CB | H |

FR-11 default is vanilla (unclamped), so this hook is **only installed when `bClampCharXPAbove100 = true`**. Fewer default hooks = fewer conflicts.

## 4. Legendary (FR-12, FR-13)

**No hook needed.** Caps live in our co-save, independent of the skill level. Legendary resets the level to `fLegendarySkillResetValue` (15) as in vanilla; the cap is untouched, so the player re-trains up to it. Vanilla requires level ≥ 100 for Legendary, which is always true when a raised cap is reached.

## 5. Effective skill for formulas (FR-24 … FR-29)

### 5.1 Central hook

| What | SE | AE | Conf. |
|---|---|---|---|
| `PlayerCharacter` ActorValueOwner `GetActorValue` (current value) | 37517 (whole fn) | 38462 (whole fn) | H |
| Skills-menu list update, to bypass the remap in UI (FR-29) | 51652 (whole fn) | 52525 (whole fn) | H |

For the player's skill actor values with current value s > 100, the hook returns a remapped value `s_eff(s)`. NPCs and non-skill AVs pass through. While the skills menu builds its list, the remap is disabled so the real level is shown.

This replaces one hook per formula with **one hook per skill**. Each skill feeds essentially one formula family, so the remap can be chosen per skill:

| Skills | Remap |
|---|---|
| One-Handed, Two-Handed, Archery, Light/Heavy Armor, Block, Smithing, Alchemy, Lockpicking, Pickpocket, Sneak | Form A: `100 + 200·(√(s/100) − 1)` |
| Enchanting | Form A, **plus charge-cost limit (see 5.3)** |
| Alteration, Conjuration, Destruction, Illusion, Restoration | Form B via inverse of the cost formula (5.2) |
| Speech | see §6 |

**Consequence:** any other reader of the player's current skill value (Papyrus `GetActorValue`, perk conditions, other mods) also sees `s_eff`. For s ≤ 100 nothing changes. Above 100 values stay ≥ 100, so vanilla perk requirements remain satisfied. Mods that test for skill > 100 see the compressed value. Acceptable; document in the readme.

### 5.2 Spell cost (FR-25)

| What | SE | AE | Conf. |
|---|---|---|---|
| Vanilla clamp of the skill to 100 in the cost formula | 26616+0x34 (`jae` → patch) | 27284+0x2C (`minss 100.0` → patch) | H |

**Vanilla already clamps magic skills at 100 in the spell cost formula.** Without removing this clamp, form B has no effect. With it removed, the remap returns

`s_eff = (1/b) · (1 − m(s))^(1/e)`, with `m(s) = 0.25 + (m₁₀₀ − 0.25)·e^(−(s−100)/τ)`

where `b` and `e` are the formula's base and exponent (vanilla 1/400 and 0.65; read from the game settings at runtime, not hardcoded). The engine then computes exactly `m(s)`. Limit: s_eff → ≈ 257 as s → ∞. Continuity at 100 checked numerically: s_eff(100) = 99.96.

Perk/equipment/dual-cast multipliers are applied by the engine after the skill factor, as FR-25 requires.

### 5.3 Enchanting charge cost — new finding

The vanilla charge-per-use formula is `base × (1 − (level × fEnchantingSkillCostBase)^fEnchantingSkillCostScale)` and **breaks at level ≥ 200** (zero, then undefined/negative cost). The reference mod caps the level in this formula at 199.

With form A, Enchanting s_eff reaches 199 at s ≈ 224, so this is reachable in tier 2.

| What | SE | AE | Conf. |
|---|---|---|---|
| Charge-points-per-use calculation (call site) | 50557+0x344 | 51449+0x314 | H |
| Max-charge region begin / end (weapon enchant) | 50557+0xE8 / +0x207 | 51449+0xE9 / +0x179 | H |

**Decision (PRD FR-30):** the charge-points-per-use call is replaced. Our implementation computes the vanilla formula for real skill ≤ 100 and form B (limit = half the value at 100) above, using the **real** Enchanting skill, not s_eff. The GMSTs `fEnchantingSkillCostBase`, `fEnchantingSkillCostScale`, `fEnchantingSkillCostMult`, `fEnchantingCostExponent` are read at runtime (SE 506021/506025/506023/506027, AE 375771/375777/375774/375780; Conf. H). Magnitude keeps form A via the AVO hook.

## 6. Barter (FR-26)

**Confidence L.** The reference mod does not touch barter. Unknown:

1. Whether vanilla clamps Speech at 100 inside the price formula (as it does for magic).
2. Where the final buy/sell price is computed, so the ≥ 1.2 ratio can be enforced.

Plan: locate the price function in Ghidra via the game settings `fBarterMin` / `fBarterMax` (cross-references to their GMST objects), on SE 1.5.97 and AE. Then either remap Speech through the AVO hook (if unclamped) or hook the price function directly and apply form B plus the 1.2 floor there.

## 7. Cap-raise transaction (FR-5 … FR-7, FR-14 … FR-17)

All engine-side via CommonLibSSE-NG, no hooks:

| Need | API |
|---|---|
| Perk points | `PlayerCharacter::perkCount` (uint8, max 255) |
| Dragon souls | actor value `ActorValue::kDragonSouls` |
| Filled black soul gems | inventory scan: `TESSoulGem` with soul level Grand and `canHoldNPCSoul`, filled via form or `ExtraSoul`. The Black Star: empty the soul, keep the item |
| Skill level / XP state | `PlayerCharacter::skills->data->skills[i]` (`level`, `xp`, `levelThreshold`) |
| Menu with dynamic text and buttons | `RE::MessageBoxData` with an `IMessageBoxCallback` (body text, button labels and paging all built at runtime) |
| Lesser power cast | `TESSpellCastEvent` / magic effect apply event for our spell |
| Persistence | SKSE serialization interface (co-save), record per skill: cap, tiers unlocked |

**No Papyrus needed.** The plugin file only needs: one lesser power (SPEL) and one script-less magic effect (MGEF). This can be generated without the Creation Kit (e.g. Mutagen/.NET), keeping the whole toolchain on Linux.

## 8. Conflict detection (FR-20)

Detect other SKSE plugins at load (`SKSE::PluginInfo` via the SKSE interface, or loaded module names) for: Skyrim Uncapper (SE/AE/Rust), eXPerience, Custom Skills Framework. Additionally, the signature check at §1 fails if another plugin already patched the site, which is a reliable second signal.

## 9. Verification plan

1. Ghidra on SE 1.5.97 and current AE `SkyrimSE.exe`, with Address Library IDs imported. Confirm every site in §1–5 matches the signatures.
2. Locate the barter price function (§6).
3. Read the actual constants of the spell cost and charge formulas from the GMSTs.

## 9a. Verification results (1.7.104)

`scripts/verify_sites.py` resolves each Address Library ID and byte-compares the
expected instructions in the real binary.

**Exact run.** Against `SkyrimSE.exe 1.7.104.0` with its own
`versionlib-1-7-104-0.bin` (Address Library All in One v13), **all nine sites
match at drift 0** — every ID resolves to exactly the address whose bytes the
table expects:

```
GetSkillCap                  0x78dd1f   ImprovePlayerSkillPoints     0x78dda1
ImproveLevelExpBySkillLevel  0x78df7b   PlayerAVOGetCurrent          0x6c4cb0
UpdateSkillList              0x979790   CapMagickaCDR                0x4471ac
CalculateChargePointsPerUse  0x9275e4   BeginMaxChargeCalculation    0x9273b9
EndMaxChargeCalculation      0x927449
```

**Corroborating run.** The same check against the `versionlib-1-7-99-0.bin`
database shipped as a CommonLibSSE test fixture reaches the identical nine
addresses via a per-region drift, which is why the table below is kept: it was
how the sites were first established, before an exact database was available,
and it is the procedure to repeat on a runtime no database covers yet.

| Site | AE id + off | Address in 1.7.104 | Drift vs 1.7.99 | Result |
|---|---|---|---|---|
| GetSkillCap | 41561 + 0x6F | 0x78dd1f | +0x260 | match |
| ImprovePlayerSkillPoints | 41561 + 0xF1 | 0x78dda1 | +0x260 | match |
| ImproveLevelExpBySkillLevel | 41561 + 0x2CB | 0x78df7b | +0x260 | match |
| PlayerAVOGetCurrent | 38462 + 0 | 0x6c4cb0 | +0x260 | match |
| UpdateSkillList | 52525 + 0 | 0x979790 | +0x260 | match |
| CapMagickaCDR | 27284 + 0x2C | 0x4471ac | 0 | match |
| CalculateChargePointsPerUse | 51449 + 0x314 | 0x9275e4 | +0x260 | match |
| BeginMaxChargeCalculation | 51449 + 0xE9 | 0x9273b9 | +0x260 | match |
| EndMaxChargeCalculation | 51449 + 0x179 | 0x927449 | +0x260 | match |

Drift is **region-local**, not a single global shift: the two functions holding
eight of the sites moved by 0x260 between 1.7.99 and 1.7.104 while the spell
cost function did not move at all. The drift is an artefact of comparing against
a neighbouring database — at runtime the player's own address library gives the
exact offsets — so it is evidence that the ids and offsets are right, not a
correction to apply.

Semantics confirmed by disassembly, not just byte equality:

- **GetSkillCap** is `call [rax+0x18]` (current skill level) → `movss xmm10, [rip+..]`
  → `comiss xmm0, xmm10` → `jae <skip XP gain>`. The constant at `0x141b5bf14`
  reads exactly **100.0**, and it is shared with the spell cost clamp. Replacing
  that load with the per-skill cap is the whole cap mechanic (FR-1 … FR-4).
- **CalculateChargePointsPerUse** loads `edx = 0x17` — `ActorValue::kEnchanting` —
  before the virtual `GetActorValue` call, then calls the charge computation,
  confirming the call site identified in §5.3.

Still outstanding from §9: the barter price function (§6) remains unlocated.

## 10. License

CommonLibSSE-NG is **GPL-3.0-or-later**. A plugin statically linking it must be distributed under a GPL-compatible license, with source available. Proposed: GPL-3.0-or-later for Beyond Mastery.
