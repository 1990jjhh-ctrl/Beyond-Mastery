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

Re-derived from SkyrimSE.exe 1.7.104 on 2026-10-23 (plan
`docs/superpowers/plans/2026-10-23-formula-scaling.md`). Everything below is
AE/1.7.x; SE 1.5.97 is out of scope (PRD §3). Player only — NPCs stay vanilla.

### 5.1 Central remap: the player's vtable slot

| What | AE | Conf. |
|---|---|---|
| `Actor::GetActorValue` (current value) | 38462 | H |
| PlayerCharacter's ActorValueOwner vtable, slot 1 (= NG `RE::VTABLE_PlayerCharacter[5]`) | 208050 | H |
| Skills menu list, `StatsMenu::UpdateSkillList(StatsMenu*)` | 52525 | H |
| Its three direct calls | 52510+0x950, 52532+0x5AC, 52591+0x246 | H |

`38462` is shared by Actor, Character and PlayerCharacter — it is slot 1 of the
ActorValueOwner vtables 207521, 207896 and 208050. Nothing in the exe calls it
directly, so replacing slot 1 of **208050** catches every read of the player's
skills and no NPC's. The callee returns `s_eff` for skills above 100: form A for
every skill except Speech, which gets the inverse of the barter formula (§6).
Spell cost applies form B in its own calls (§5.2).

The engine's XP routine reads the skill through slot **3** (`call [rax+0x18]`,
`GetBaseActorValue`), so leveling and our cap hook never see `s_eff`.

While the skills menu builds its list the remap is off (FR-29). The function is
not patched itself: its three calls are redirected to a wrapper that opens a
scope around the original, each checked to still target 52525.

### 5.2 The engine's skill clamp and spell cost (FR-25)

| What | AE | Conf. |
|---|---|---|
| `ActorValueOwner::GetClampedActorValue` (NG `RELOCATION_ID(26616, 27284)`) | 27284 | H |
| Spell cost skill factor | 26505 | H |
| Its two calls (`call` at pattern +11 / +12) | 11017+0x38, 11494+0x4C | H |

`27284` is **not** a spell-cost clamp: it is the engine's generic clamp of an
actor value by its `ActorValueInfo::flags` — `kClampAsSkill` (1<<4) to [0, 100],
`kClampAsSpecialStat` (1<<3) to [0 or 1, 10] (1 with
`kSpecialStatClampsAsNonZero`, 1<<2). Its 13 callers include Pickpocket,
Lockpicking and Sneak (constant AV 0xD/0xE/0xF), spell cost (11017, 11494 →
26505) and barter Speech (§6). Without lifting the ceiling, `s_eff` above 100
would be cut back to 100 for all of them. The function is replaced whole
(`jmp` at its entry, bytes `40 53 48 83 EC 20 48 8B 01 48 63 DA 8B D3 FF 50 08`):
vanilla rules for everyone, except that the player's skills are only kept ≥ 0.

Spell cost (26505) for the player is
`cost · fMagicCasterPCSkillCostMult · (1 − (int(skill) · fMagicCasterPCSkillCostBase)^fMagicPCSkillCostScale)`;
NPCs use `fMagicCasterSkillCostBase` / `fMagicSkillCostScale` /
`fMagicCasterSkillCostMult`. The skill arrives **truncated to int**, and it is
whatever skill the effect is tied to — any of the 18, not only the magic schools.

Every skill reaches 26505 as `int(form A)`, because the remap is form A for all
but Speech. Both calls are wrapped: the wrapper recovers the real skill
(`RealSkillFromSqrt`), applies form B `m(s) = 0.25 + (m₁₀₀ − 0.25)·e^(−(s−100)/τ)`
with the PC GMSTs read at runtime (Skyrim.esm: base 0.0025, scale 0.65), and hands
the engine `floor(InverseVanilla(m(s)))`, so the engine computes `m(s)` itself —
the same curve for a magic school and for a modded effect tied to, say, Sneak.
The int is also the resolution near the limit: real skill 900 and 2000 both give
engine skill 256. NPC skills arrive clamped to 100 and pass through unchanged.
Perk, equipment and dual-cast multipliers apply afterwards, as FR-25 requires.

The shared `GetActorValue` (38462) is byte-checked too: a mod that detoured it
would stack its own remap under ours, so formula scaling refuses to install and
the log names the conflict.

### 5.3 Enchanting charge cost (FR-30)

| What | AE | Conf. |
|---|---|---|
| Charge per use, `(1 − (skill·fEnchantingSkillCostBase)^fEnchantingSkillCostScale) · fEnchantingSkillCostMult · base^fEnchantingCostExponent` | 26494 | H |
| Its only call (pattern 51449+0x314, `E8` at +31) | 51449+0x333 | H |

The skill passed is `GetActorValue(Enchanting)`, i.e. `s_eff`; the replacement
ignores it and uses the **real** skill (FR-30): vanilla up to 100, above it the
skill-free part `fEnchantingSkillCostMult · base^fEnchantingCostExponent`
(the original called with skill 0) times form B. Enchanting magnitude keeps form A
through the remap.

Max charge (51449+0xE9 … +0x179) is the soul's charge through perk entry 0x4F
(`ModSoulGemEnchanting`); it never reads the skill and needs no hook.

## 6. Barter (FR-26)

| What | AE | Conf. |
|---|---|---|
| Barter menu price setup | 50945 | H |
| Skill price factors | 26464 | H |
| Final clamp after perks | 50945+0x80A (64 bytes) | H |

`50945` reads Speech as `GetClampedActorValue(player, 0x11)` — **vanilla clamps
it at 100** (§5.2). `26464` then computes
`buy = max(fBarterMax − (fBarterMax − fBarterMin) · s · 0.01, fBarterBuyMin)` and
`sell = min(1/buy, fBarterSellMax)`. Perk entries 8 (`ModBuyPrices`) and 0x3C
(`ModSellPrices`) follow, then the final clamp
`buy = max(fMinBuyMult, buy, 1.0)` → `[rbp+0xA0]`,
`sell = min(fMaxSellMult, sell, 1.0)` → `[rbp+0x98]`; afterwards only `xmm1`
(= buy) is live.

Form B reaches the skill factor through the remap: Speech's `s_eff` is the
inverse of the linear formula for an asymptote towards `√fBarterMinRatio`
(before perks the ratio is `buy/sell = buy²`). The final clamp is replaced by a
stub that runs the vanilla clamp and then floors `buy ≥ fBarterMinRatio · sell`,
which holds after perks and Fortify Barter.

## 7. Cap-raise transaction (FR-5 … FR-7, FR-14 … FR-17, FR-31)

### 7.1 Entry point: vanilla Legendary (PRD v1.4)

Pressing **[Legendary]** in the skill tree makes the engine build a
`RE::LegendarySkillResetConfirmCallback` and show a yes/no message box with it.
The callback carries the skill and performs the reset on confirmation:

| Member | Offset | Meaning |
|---|---|---|
| `menu` | +0x10 | the `StatsMenu` that raised it |
| `skill` | +0x1C | the `ActorValue` being made Legendary |
| `Run(std::uint8_t button)` | vtable slot 1 | performs the reset for the confirming button |

Address Library: vtable `VariantID(269983, 215997, …)`, RTTI `(688914, 396769, …)`;
size 0x20. Offered at any level ≥ 100 — confirmed in-game on 1.7.104 with
One-Handed at 110.

The menu replaces that box (FR-14, FR-15): **Reset Skill** delegates to the
vanilla callback's `Run` with the confirming button, so it stays exactly vanilla
(FR-31); **Upgrade** runs our transaction instead.

Confidence **H (own disassembly)**. Cross-referencing the vtable (every
`lea reg, [rip+vtable]` in `.text`) finds exactly two construction sites, both in
`StatsMenu`, both building the callback and passing it straight to one call:

| Site | AE id + off (pattern start) | Address in 1.7.104 | Reads the skill from |
|---|---|---|---|
| LegendaryBoxA | 52510 + 0x5E3 | 0x977033 | `StatsMenu + 0x1c0` |
| LegendaryBoxB | 52520 + 0x25C | 0x9787dc | `StatsMenu + 0x190` |

Both call `MessageBoxMenu::Create(message, callback, 0, 25, 10, buttons)` — id
`AE_CHECK(1.6.1130, 52270, 442726)` — 15 bytes after the pattern start, so the
message, the button array and the callback all pass through that one call.
Replacing it with a `write_call<5>` call-site hook (a typed C++ function of the
same signature; no hand-written assembly) replaces the box. Both sites verify at
drift 0 against `versionlib-1-7-104-0.bin` (`scripts/verify_sites.py`).

`Run` (vtable slot 1, `0x982160`) acts only for **button 1** — the box's
buttons are *[No] [Yes]*, so Yes is index 1 — after a precondition call
(`id 82074`) that can make it do nothing. It does **not** reset on the first
call: vanilla Legendary confirms twice. Both call sites build the callback with
`unk18` (+0x18) = 1; while it is set, `Run` clears it and shows a **second box**
with the same callback, whose text is GMST `sLegendaryResetSecondConfirm`
(in-game: *"Making a skill Legendary will set it to 15 and return its perks. Are
you sure?"*; the exe's built-in fallback, *"If you do this you will lose your
perks and be reset to 15?"*, is overridden by the localised strings). Only that
box's Yes runs `Run` again with `unk18 == 0` and resets the skill.

So Reset Skill delegates with `Run(1)` and keeps vanilla's second box (FR-31).
Because the reset happens inside that later box, gates are cleared by the cap
hook once the skill is seen below 100, not by a level check around `Run`. The
confirmation text (FR-31) is set on `sLegendaryResetSecondConfirm` only while `Run` builds the
box — `Run` copies the text — and restored straight after. Never use
`Setting::SetString` for it: it `free()`s the old string, which is exe- or
engine-owned memory, and with a static CRT that is our heap freeing theirs.

Nothing in the skill tree's Flash UI is touched (FR-17).

### 7.2 Transaction

All engine-side via CommonLibSSE-NG:

| Need | API |
|---|---|
| Perk points | `PlayerCharacter::perkCount` (uint8, max 255). Layout changed at 1.6.629 — reach it only through NG's runtime accessor (development.md, "Working on hooks") |
| Dragon souls | actor value `ActorValue::kDragonSouls` |
| Filled black soul gems | inventory scan: `TESSoulGem` with soul level Grand and `canHoldNPCSoul`, filled via form or `ExtraSoul`. The Black Star: empty the soul, keep the item |
| Skill level / XP state | `PlayerCharacter::skills->data->skills[i]` (`level`, `xp`, `levelThreshold`). When a level-up reaches the cap, the engine sets `levelThreshold` to 0 (1.7.104: 0x14078df1f). At the cap the XP routine adds none, but `player.advskill` still accumulates `xp`. After a raise, restore the threshold and zero `xp`, or the next level (or many) is free (FR-8) |
| Menu with dynamic text and buttons | `RE::MessageBoxData` with an `IMessageBoxCallback` (body text and button labels built at runtime; at most three buttons, so no paging) |
| Persistence | SKSE serialization interface (co-save), record per skill: cap, gates paid this climb |

**No Papyrus and no plugin file.** The lesser power of PRD ≤ v1.3, with its
SPEL/MGEF records, is gone.

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
`versionlib-1-7-104-0.bin` (Address Library All in One v13), **all 13 sites
match at drift 0** (15 since the spell-cost calls):

```
GetSkillCap                  0x78dd1f   ImprovePlayerSkillPoints     0x78dda1
ImproveLevelExpBySkillLevel  0x78df7b   PlayerAVOGetCurrent          0x6c4cb0
UpdateSkillList              0x979790   SkillListCallA               0x9773a0
SkillListCallB               0x97cf7c   SkillListCallC               0x9823a6
GetClampedActorValue         0x447180   CalculateChargePointsPerUse  0x9275e4
LegendaryBoxA                0x977033   LegendaryBoxB                0x9787dc
BarterFinalClamp             0x900d8a   SpellCostCallA               0x148a98
SpellCostCallB               0x15550c
```

**Corroborating run.** The same check against the `versionlib-1-7-99-0.bin`
database shipped as a CommonLibSSE test fixture reaches the same addresses via a
per-region drift (`tests/test_verify_sites.py` repeats it): +0x260 for the sites
in the skill-XP and enchanting functions, 0 for `GetClampedActorValue`. Drift is
region-local — an artefact of comparing against a neighbouring database, not a
correction to apply; at runtime the player's own address library gives the exact
offsets. Short patterns must be unique within the ±0x1000 search window, which
is why the three skills-menu call patterns run on past the call.

Semantics confirmed by disassembly, not just byte equality:

- **GetSkillCap** is `call [rax+0x18]` (`GetBaseActorValue`, the base skill
  level) → `movss xmm10, [rip+..]` → `comiss xmm0, xmm10` → `jae <skip XP gain>`.
  The constant at `0x141b5bf14` reads exactly **100.0**, and it is shared with the
  engine's skill clamp. Replacing that load with the per-skill cap is the whole
  cap mechanic (FR-1 … FR-4).
- **CalculateChargePointsPerUse** loads `edx = 0x17` — `ActorValue::kEnchanting` —
  before the virtual `GetActorValue` call, then calls 26494 (§5.3).
- The sites in §5 and §6 were established by disassembly first (2026-10-23) and
  their GMST operands identified through each `Setting` object's name pointer.

## 10. License

CommonLibSSE-NG is **GPL-3.0-or-later**. A plugin statically linking it must be distributed under a GPL-compatible license, with source available. Proposed: GPL-3.0-or-later for Beyond Mastery.
