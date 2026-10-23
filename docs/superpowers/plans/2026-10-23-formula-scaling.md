# Formula Scaling Above 100 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Skill effects keep growing above 100, sub-linearly (form A) or towards a limit (form B), for the player only. This covers FR-24…FR-30, FR-11, and FR-10 as decided below.

**Architecture:** There is one remap point. Slot 1 of the player's ActorValueOwner vtable returns an effective skill `s_eff` for skills above 100. Every formula that reads the player's skill sees `s_eff`. Three engine spots stand in its way, and each is handled at the narrowest site:
- the generic `GetClampedActorValue`, which clamps every skill to 100;
- enchanting charge cost, which is computed from the **real** skill (FR-30);
- the final barter clamp, which gets the 1.2 ratio floor (FR-26).

The skills menu bypasses the remap (FR-29). All math is game-free in `Curves.h`, with native tests.

**Tech Stack:** C++23 plugin (CommonLibSSE-NG, xbyak stubs), C++20 native tests, Python site verifier.

**Spec:** [PRD.md](../../../PRD.md) v1.8 — §6.3 (FR-10, FR-11) and §6.9 (FR-24…FR-30). The binary facts below were re-derived from SkyrimSE.exe 1.7.104 on 2026-10-23 and **supersede hook-points.md §5.2, §5.3 and §6**, which Task 2 rewrites.

## Binary facts (1.7.104, AE ids)

| Fact | Where |
|---|---|
| `38462` is `Actor::GetActorValue`, shared by Actor/Character/PlayerCharacter. It is slot 1 of AVO vtables 207521, 207896 and **208050 (player)** = NG `RE::VTABLE_PlayerCharacter[5]`. It has **no** direct calls, so the vtable slot catches every player read. | vtable 208050 slot 1 |
| `27284` is `ActorValueOwner::GetClampedActorValue` (NG `RELOCATION_ID(26616, 27284)`), **not** a spell-cost clamp: `v = this->GetActorValue(av)`, then by `ActorValueInfo::flags`: `kClampAsSkill` (1<<4) → `[0, 100]`; `kClampAsSpecialStat` (1<<3) → `[kSpecialStatClampsAsNonZero (1<<2) ? 1 : 0, 10]`; otherwise unchanged. 13 callers, among them Pickpocket/Lockpicking/Sneak (constant AV 0xD/0xE/0xF), spell cost (`11017`, `11494` → `26505`) and barter Speech (`50945+0x7a0`). Entry bytes: `40 53 48 83 EC 20 48 8B 01 48 63 DA 8B D3 FF 50 08`. | 27284+0 |
| Spell cost skill factor `26505`: `(1 − (int(skill) · fMagicCasterPCSkillCostBase)^fMagicPCSkillCostScale) · fMagicCasterPCSkillCostMult · cost` for the player. The NPC set is `fMagicCaster­SkillCostBase`/`fMagicSkillCostScale`. The skill arrives **truncated to int**. | 26505 |
| Enchanting charge per use `26494`: `(1 − (skill · fEnchantingSkillCostBase)^fEnchantingSkillCostScale) · fEnchantingSkillCostMult · base^fEnchantingCostExponent`. Called **only** from `51449+0x333` (`E8` at the end of the existing `kCalculateChargePointsPerUse` pattern, pattern offset 31), with the skill from `GetActorValue(Enchanting)`. | 51449+0x333 |
| Max charge (`51449+0xE9…0x179`) is the soul's charge passed through perk entry `0x4F`. It does **not** read the skill, so it needs no hook. | — |
| Barter `50945`: Speech = `GetClampedActorValue(player, 0x11)`. `26464` computes `buy = max(fBarterMax − (fBarterMax − fBarterMin) · s · 0.01, fBarterBuyMin)` and `sell = min(1/buy, fBarterSellMax)`. Perk entries 8 / 0x3C follow, then the **final clamp** at `50945+0x80A` (64 bytes): `buy = max(fMinBuyMult, buy, 1.0)`, `sell = min(fMaxSellMult, sell, 1.0)`, stored at `[rbp+0xA0]` / `[rbp+0x98]`. After it only `xmm1` (= buy) is live. | 50945+0x80A |
| Skills menu list `52525` takes `(StatsMenu* rcx)` and returns nothing used. It has 3 direct calls: `52510+0x950` `48 8B CE E8 …`, `52532+0x5AC` `48 8B CF E8 …`, `52591+0x246` `48 8B 4E 10 E8 …`. | call sites |
| Character XP per level-up: at `41561+0x2CB`, `subss xmm1, xmm2` yields the **new level**, then `mulss xmm1, [fXPPerSkillRank]` (12 bytes). `rax` is live across it. | 41561+0x2CB |

## Global Constraints

- AE 1.6.x / 1.7.x only. SE 1.5.97 is out of scope (PRD §3): every new site has SE location `{0, 0}` and reports `kNotOnThisRuntime`.
- Player only: NPC values, clamps and prices stay byte-for-byte vanilla.
- For `s ≤ 100` every formula is unchanged (§6.9). Form A is `100 + 200·(√(s/100) − 1)`. Form B matches value and slope at 100.
- Defaults (FR-28, already in `Config`): `fSpellCostLimit` 0.25, `fBarterMinRatio` 1.2, `fEnchantChargeLimitFraction` 0.5, `bEnableFormulaScaling` true. With scaling disabled, **no** scaling hook is installed.
- Formula constants are read from GMSTs at `kDataLoaded` (after Skyrim.esm's overrides) and cached. They are never hardcoded.
- Every patch site is resolved by Address Library id and byte-checked; call sites are also target-checked (as `InstallLegendaryMenu` does). On mismatch: log, set the conflict flag, skip.
- Struct members that move between runtimes are reached only through NG runtime accessors (development.md, "Working on hooks").

## Decisions taken in this plan (approved by Joel, 2026-10-23)

1. **The vtable slot, not a byte patch of `38462`.** Patching the shared function would remap NPCs too, and NPCs stay purely vanilla (Joel's ruling). Cost if wrong: a mod that calls `38462` directly would bypass the remap; none does in vanilla.
2. **`GetClampedActorValue` is reimplemented** (`write_branch<5>` at its entry). The player's `kClampAsSkill` values are clamped at 0 only; everything else follows the vanilla rules above. This is what lets form A reach Lockpicking/Pickpocket/Sneak and form B reach spell cost and barter. FR-27's formula-level clamps (pickpocket max chance, etc.) are untouched.
3. **The spell-cost and barter limits come through `s_eff`** (the inverse of the vanilla formula), not through extra hooks. The barter limit for the pre-perk buy factor is `√fBarterMinRatio` (≈ 1.095), because the ratio before perks is `buy/sell = buy²`. The final clamp then enforces the ratio after perks.
4. **The `kBeginMaxChargeCalculation` / `kEndMaxChargeCalculation` sites are deleted**: max charge never reads the skill. `kCapMagickaCDR` becomes `kGetClampedActorValue`. `kUpdateSkillList` (whole function) becomes three call sites.
5. **FR-10 is deferred, with a one-time log warning.** Float accumulation becomes inexact only once a skill's threshold passes 2²⁴. For Two-Handed (improveMult 2, `fSkillUseCurve` 1.95) that is level ≈ 3,550. Implementing a double accumulator now would mean an always-on XP hook for a level nobody reaches. Instead, the cap hook logs once per skill when `Curves::ThresholdNeedsDouble` first holds. Approved: floats stay.
6. **FR-28 "scaling form per formula" is the master switch plus the three limits.** No per-skill form choice. Approved.

## Review Focus

1. **NPCs reading a clamped skill** must get exactly vanilla `[0, 100]`. → `TestClampMatchesVanillaForNPCs` (Task 1).
2. **Spell cost with the engine's int truncation** must still meet criterion 8 (0.29 ± 0.01 at 400, never below 0.25). → `TestSpellCostThroughIntTruncation` (Task 1).
3. **Barter after perks** (Haggling, Allure, Fortify Barter) pushing buy towards sell must still give buy ≥ 1.2 × sell. → `TestBarterFinalClampFloorsRatio` (Task 1).
4. **Enchanting at real skill ≤ 100** must equal vanilla exactly, even though `s_eff` would be passed in. → `TestEnchantChargeVanillaUpTo100` (Task 1), and the real-skill path in Task 5.
5. **Limits at their config extremes** (`fSpellCostLimit` 0 or 0.59, `fBarterMinRatio` 1.0) must give a finite `s_eff ≥ 100` that never decreases as `s` grows. → `TestRemapsFiniteAtConfigExtremes` (Task 1).

---

### Task 1: Game-free math for the remaps and clamps

**Files:**
- Modify: `src/Curves.h`
- Test: `tests/test_curves.cpp`

**Interfaces:**
- Produces:
  - `double BarterFormula::InverseVanilla(double a_factor) const noexcept`: the inverse of `Vanilla`, `100 · (factorMax − f) / (factorMax − factorMin)`.
  - `double BarterFormula::EffectiveSkill(double a_skill, double a_limit) const noexcept`: identity for `s ≤ 100`, otherwise `InverseVanilla(MakeAsymptote(a_limit)(s))`.
  - `struct BarterPrices { double buy; double sell; }` and `BarterPrices BarterFinalClamp(double a_buy, double a_sell, double a_minBuyMult, double a_maxSellMult, double a_minRatio) noexcept`: the vanilla final clamp (`buy = max(minBuyMult, buy, 1)`, `sell = min(maxSellMult, sell, 1)`), then `buy = ClampBuyPrice(buy, sell, minRatio)`.
  - `inline constexpr std::uint32_t kClampAsSkill = 1u << 4, kClampAsSpecialStat = 1u << 3, kSpecialStatClampsAsNonZero = 1u << 2;`
  - `double ClampActorValue(double a_value, std::uint32_t a_flags, bool a_liftSkillCeiling) noexcept`: the rules from the binary-facts table. With `a_liftSkillCeiling`, `kClampAsSkill` gives `max(v, 0)`.

- [ ] **Step 1: Write the failing tests**

```cpp
static void TestBarterEffectiveSkill()          // fBarterMax 3.3, fBarterMin 2.0, limit sqrt(1.2)
{
	const BM::Curves::BarterFormula b{ 3.3, 2.0 };
	const double L = std::sqrt(1.2);
	CHECK_NEAR(b.EffectiveSkill(100.0, L), 100.0, 1e-9);
	CHECK_NEAR(b.EffectiveSkill(150.0, L), 135.664, 0.01);
	CHECK_NEAR(b.EffectiveSkill(200.0, L), 153.049, 0.01);
	CHECK_NEAR(b.EffectiveSkill(1e4, L), 169.581, 0.01);
	CHECK_NEAR((b.EffectiveSkill(100.001, L) - 100.0) / 0.001, 1.0, 0.001);  // C1 at 100
	CHECK_NEAR(b.Vanilla(b.EffectiveSkill(1e4, L)), L, 1e-6);
}
static void TestBarterFinalClampFloorsRatio()
{
	auto p = BM::Curves::BarterFinalClamp(1.05, 0.95, 1.0, 1.0, 1.2);  // perks pushed buy down
	CHECK_NEAR(p.buy, 1.14, 1e-9);  CHECK_NEAR(p.sell, 0.95, 1e-9);
	p = BM::Curves::BarterFinalClamp(0.8, 0.9, 1.0, 1.0, 1.2);         // vanilla floor, then the ratio
	CHECK_NEAR(p.buy, 1.08, 1e-9);  CHECK_NEAR(p.sell, 0.9, 1e-9);
	p = BM::Curves::BarterFinalClamp(2.0, 0.5, 1.0, 1.0, 1.2);         // already fine: unchanged
	CHECK_NEAR(p.buy, 2.0, 1e-9);   CHECK_NEAR(p.sell, 0.5, 1e-9);
}
static void TestClampMatchesVanillaForNPCs()
{
	using namespace BM::Curves;
	CHECK_NEAR(ClampActorValue(150.0, kClampAsSkill, false), 100.0, 0);
	CHECK_NEAR(ClampActorValue(-5.0, kClampAsSkill, false), 0.0, 0);
	CHECK_NEAR(ClampActorValue(150.0, kClampAsSkill, true), 150.0, 0);
	CHECK_NEAR(ClampActorValue(-5.0, kClampAsSkill, true), 0.0, 0);
	CHECK_NEAR(ClampActorValue(12.0, kClampAsSpecialStat, false), 10.0, 0);
	CHECK_NEAR(ClampActorValue(0.0, kClampAsSpecialStat | kSpecialStatClampsAsNonZero, false), 1.0, 0);
	CHECK_NEAR(ClampActorValue(0.0, kClampAsSpecialStat, false), 0.0, 0);
	CHECK_NEAR(ClampActorValue(500.0, 0, false), 500.0, 0);
}
static void TestSpellCostThroughIntTruncation()  // PC set: base 0.0025, scale 0.65, limit 0.25
{
	const BM::Curves::SpellCostFormula f{ 0.0025, 0.65 };
	const auto engine = [&](double s) { return f.Vanilla(std::floor(f.EffectiveSkill(s, 0.25))); };
	CHECK_NEAR(engine(100.0), f.Vanilla(100.0), 1e-12);
	CHECK_NEAR(engine(400.0), 0.284, 0.01);                   // criterion 8: 0.29 +- 0.01
	for (double s = 100.0; s <= 1e5; s *= 1.5) CHECK(engine(s) >= 0.25);
}
static void TestEnchantChargeVanillaUpTo100()   // base 0.005, scale 0.5, limit 0.5 x value at 100
{
	const auto e = BM::Curves::EnchantChargeFormula(0.005, 0.5);
	const double L = 0.5 * e.Vanilla(100.0);
	for (double s : { 15.0, 50.0, 99.0, 100.0 }) CHECK_NEAR(e.Factor(s, L), e.Vanilla(s), 1e-12);
	CHECK_NEAR(e.Factor(125.0, L), 0.227, 0.001);
	CHECK_NEAR(e.Factor(150.0, L), 0.190, 0.001);
	CHECK_NEAR(e.Factor(200.0, L), 0.160, 0.001);
	CHECK(e.Factor(1000.0, L) >= 0.146);                       // criterion 10
}
static void TestRemapsFiniteAtConfigExtremes()
{
	const BM::Curves::SpellCostFormula f{ 0.0025, 0.65 };
	const BM::Curves::BarterFormula b{ 3.3, 2.0 };
	for (double limit : { 0.0, 0.59 }) {
		double prev = 100.0;
		for (double s = 100.0; s <= 1e5; s *= 1.3) {
			const double e = f.EffectiveSkill(s, limit);
			CHECK(std::isfinite(e) && e >= 100.0 && e >= prev - 1e-9);
			prev = e;
		}
	}
	CHECK_NEAR(b.EffectiveSkill(500.0, std::sqrt(1.0)), b.InverseVanilla(1.0), 1.0);
}
```

Call all six from `main()`.

- [ ] **Step 2: Run it and watch it fail**

Run: `scripts/test.sh`
Expected: a compile error, because `EffectiveSkill`, `BarterFinalClamp` and `ClampActorValue` are not declared yet.

- [ ] **Step 3: Implement the Interfaces above in `src/Curves.h`**

`BarterFormula` gains its two members beside `MakeAsymptote`. `BarterFinalClamp` goes under the existing `ClampBuyPrice` and reuses it.

- [ ] **Step 4: Run the tests**

Run: `scripts/test.sh`
Expected: `All curve tests passed.` and every other suite green.

- [ ] **Step 5: Commit** — `feat: remap, barter-clamp and value-clamp math for formula scaling (FR-24..30)`

---

### Task 2: Site table and hook-points.md match the binary

**Files:**
- Modify: `src/HookSites.h`, `tests/test_hooksites.cpp`, `docs/hook-points.md` (§5.1–5.3, §6, §9a)

**Interfaces:**
- Produces, in `BM::HookSites` (each with SE location `{0, 0}`, SE pattern `""` and `verified = true`):

| Name | AE location | AE pattern |
|---|---|---|
| `kGetClampedActorValue` (replaces `kCapMagickaCDR`) | `{27284, 0}` | `40 53 48 83 EC 20 48 8B 01 48 63 DA 8B D3 FF 50 08` |
| `kSkillListCallA` | `{52510, 0x950}` | `48 8B CE E8 ?? ?? ?? ??` |
| `kSkillListCallB` | `{52532, 0x5AC}` | `48 8B CF E8 ?? ?? ?? ??` |
| `kSkillListCallC` | `{52591, 0x246}` | `48 8B 4E 10 E8 ?? ?? ?? ??` |
| `kBarterFinalClamp` | `{50945, 0x80A}` | the 64 bytes from the binary-facts table, with each `rip` disp32 as `?? ?? ?? ??`: `F3 0F 10 0D ?? ?? ?? ?? F3 0F 5F 8D A0 00 00 00 F3 0F 5F 0D ?? ?? ?? ?? F3 0F 11 8D A0 00 00 00 F3 0F 10 05 ?? ?? ?? ?? F3 0F 5D 85 98 00 00 00 F3 0F 5D 05 ?? ?? ?? ?? F3 0F 11 85 98 00 00 00` |

  - `kUpdateSkillList` stays (`{52525, 0}`); it is now the call *target* those three sites are checked against.
  - `kChargeCallOffset = 31` (the `E8` inside `kCalculateChargePointsPerUse`). Call offsets `kSkillListCallOffsetA/B = 3`, `C = 4`. `kBarterFinalClampLength = 64`.
  - Delete `kBeginMaxChargeCalculation` and `kEndMaxChargeCalculation`.

- [ ] **Step 1: Write the failing tests** in `tests/test_hooksites.cpp`. Update `TestLegendaryBoxSites`' count to the new `kAllSites.size()` (13). Add:

```cpp
static void TestScalingSites()
{
	using namespace BM::HookSites;
	CHECK(kGetClampedActorValue.ae.id == 27284 && kGetClampedActorValue.ae.offset == 0);
	CHECK(kBarterFinalClamp.ae.id == 50945 && kBarterFinalClamp.ae.offset == 0x80A);
	CHECK(kBarterFinalClamp.patternAE.size() == kBarterFinalClampLength * 3 - 1);
	CHECK(kCalculateChargePointsPerUse.patternAE.substr(kChargeCallOffset * 3, 2) == "E8");
	CHECK(kSkillListCallA.patternAE.substr(kSkillListCallOffsetA * 3, 2) == "E8");
	CHECK(kSkillListCallB.patternAE.substr(kSkillListCallOffsetB * 3, 2) == "E8");
	CHECK(kSkillListCallC.patternAE.substr(kSkillListCallOffsetC * 3, 2) == "E8");
	for (const auto* s : { &kGetClampedActorValue, &kBarterFinalClamp, &kSkillListCallA, &kSkillListCallB, &kSkillListCallC })
		CHECK(!ExistsOn(*s, false) && ExistsOn(*s, true));
}
```

- [ ] **Step 2: Run it and watch it fail** — `scripts/test.sh`. Expected: a compile error on the undeclared sites.
- [ ] **Step 3: Edit `src/HookSites.h`** as in the Interfaces. Each site's comment states its fact from the binary-facts table.
- [ ] **Step 4: Verify against the exe** — `scripts/test.sh` (it runs `test_verify_sites.py`) and `python3 scripts/verify_sites.py --exe "<Skyrim>/SkyrimSE.exe" --db "<Skyrim>/Data/SKSE/Plugins/versionlib-1-7-104-0.bin"`. Expected: every site matches at drift 0.
- [ ] **Step 5: Rewrite hook-points.md** §5.1 (vtable slot, no direct calls), §5.2 (`GetClampedActorValue` and the spell-cost function and GMSTs), §5.3 (charge call `26494`; max charge needs no hook), §6 (barter, now confidence H), and §9a (results). Use the binary-facts table as the source.
- [ ] **Step 6: Commit** — `refactor: scaling sites match 1.7.104 (GetClampedActorValue, skills-menu calls, barter clamp)`

---

### Task 3: Stubs for the barter clamp and the character-XP clamp

**Files:**
- Modify: `src/Stubs.h`, `tests/test_stubs.cpp`

**Interfaces:**
- Consumes: `kBarterFinalClampLength` (Task 2).
- Produces:
  - `struct BarterClampStub : Xbyak::CodeGenerator { explicit BarterClampStub(std::uintptr_t a_callee); }`. The callee is `void(float* a_buy, float* a_sell)`. The stub reserves `kStubReserve` and computes `rcx = rbp + 0xA0`, `rdx = rbp + 0x98`. `rbp` is the engine's frame register and is unaffected by our push. It calls the callee, reloads `xmm1` from `[rbp + 0xA0]`, frees the reserve and returns.
  - `struct CharXPClampStub : Xbyak::CodeGenerator { CharXPClampStub(std::uintptr_t a_xpPerSkillRank); }`. It emits `subss xmm1, xmm2`, then `minss xmm1, [100.0]` (the constant is embedded after the code), then `mulss xmm1, [a_xpPerSkillRank]` through a pushed and restored scratch register (**not `rax`**, which is live), then `ret`. It replaces 12 bytes: `kCharXPLength = 12`.

- [ ] **Step 1: Write the failing tests**, in the style of `TestXPGainStubReadsTheEngineFrame`. Emit each stub and decode its bytes.
  - `TestBarterClampStubPointsAtTheEngineFrame`: it contains `lea rcx, [rbp+0xA0]`, `lea rdx, [rbp+0x98]` and `movss xmm1, [rbp+0xA0]`; it keeps rsp 16-byte aligned at the call (`kStubReserve` + return address); and it ends in `ret`.
  - `TestCharXPClampStubPreservesRax`: no instruction writes `rax`, the embedded constant is `100.0f`, and the order is `subss` → `minss` → `mulss`.
  - Extend `TestStubsFitTheTrampolineBudget` with both sizes, and raise `trampolineSize` in `main.cpp` if the budget test says so.
- [ ] **Step 2: Run them and watch them fail** — `scripts/build.sh` (the stub tests need xbyak from the configure step). Expected: a compile error.
- [ ] **Step 3: Implement both stubs.**
- [ ] **Step 4: Run** — `scripts/build.sh`. Expected: `All stub tests passed.`
- [ ] **Step 5: Commit** — `feat: xbyak stubs for the barter final clamp and the character-XP clamp`

---

### Task 4: The effective-skill remap, the clamp and the skills menu (FR-24, FR-25, FR-27, FR-29)

**Files:**
- Create: `src/Scaling.h`, `src/Scaling.cpp`
- Modify: `src/Hooks.cpp`, `src/Hooks.h`, `CMakeLists.txt` (source list), `docs/development.md` (layout table and the "Two scaling mechanisms" section)

**Interfaces:**
- Consumes: Task 1's math, Task 2's sites, `Skills::ScalingOf`, `Config`.
- Produces, in `BM::Scaling`:
  - `void CacheSettings()`: reads at `kDataLoaded` `fMagicCasterPCSkillCostBase`, `fMagicPCSkillCostScale`, `fBarterMax`, `fBarterMin`, `fEnchantingSkillCostBase`, `fEnchantingSkillCostScale`, `fMinBuyMult` and `fMaxSellMult`, and logs them once. Missing settings fall back to the esm values from this plan and log a warning.
  - `float EffectiveSkill(RE::ActorValue a_av, float a_current) noexcept`: identity for `a_current ≤ 100` or for non-skills. Otherwise `kSqrt` → `SqrtEffectiveSkill`, `kSpellCost` → `SpellCostFormula{base, scale}.EffectiveSkill(s, spellCostLimit)`, `kBarter` → `BarterFormula{max, min}.EffectiveSkill(s, √barterMinRatio)`.
  - `float GetActorValue(RE::ActorValueOwner* a_this, RE::ActorValue a_av)`: the vtable callee. It calls the saved original, then applies `EffectiveSkill` unless a skills-menu scope is open.
  - `float RealActorValue(RE::ActorValue a_av)`: the player's unremapped current value, via the saved original.
  - `float GetClampedActorValue(RE::ActorValueOwner* a_this, RE::ActorValue a_av)`: `ClampActorValue(a_this->GetActorValue(a_av), info->flags, a_this == player AVO)`.
  - `void UpdateSkillList(RE::StatsMenu* a_menu)`: opens an RAII scope (an atomic counter, because GetActorValue runs on several threads), then calls `52525`.
- In `Hooks.cpp`: `bool InstallScaling()`, called from `InstallAll` after the cap hook when `enableFormulaScaling` is set. In order:
  1. Verify vtable 208050 slot 1 equals the address of `38462`, then `write_vfunc`.
  2. Byte-check `kGetClampedActorValue`, then `write_branch<5>`.
  3. Target-check the three skills-menu calls against `52525`, then `write_call<5>`.

  All or nothing, each failure logged as in `InstallLegendaryMenu`. Add `ScalingInstalled()` to the summary line.

- [ ] **Step 1: Write the failing check.** The math is covered by Task 1. This task's gate is the build plus a startup self-check: `InstallScaling` logs `EffectiveSkill` for every skill at 100, 150 and 400, so a wrong form or a wrong setting shows in the log.
- [ ] **Step 2: Implement** `Scaling.*` and `InstallScaling` per the Interfaces. Take the player AVO through `RE::PlayerCharacter::GetSingleton()->AsActorValueOwner()`, and the AV info through `RE::ActorValueList::GetActorValueInfo`.
- [ ] **Step 3: Build** — `scripts/build.sh`. Expected: `BUILD_EXIT=0`, tests green, runtime gate OK, format clean (`scripts/format.sh --check`).
- [ ] **Step 4: Commit** — `feat: effective skill above 100 for the player (FR-24, FR-25, FR-27, FR-29)`

---

### Task 5: Enchanting charge from the real skill (FR-30)

**Files:** Modify `src/Scaling.cpp`, `src/Scaling.h`, `src/Hooks.cpp`

**Interfaces:**
- Consumes: `RealActorValue`, `kCalculateChargePointsPerUse`, `kChargeCallOffset`.
- Produces: `float ChargePerUse(float a_baseCost, float a_skill)`, the `write_call<5>` callee replacing `26494`.
  - It ignores `a_skill`, which is `s_eff`, and takes `s = RealActorValue(kEnchanting)`.
  - For `s ≤ 100` it returns `original(a_baseCost, s)`, exactly vanilla.
  - Above 100 it returns `original(a_baseCost, 0) · EnchantChargeFormula(base, scale).Factor(s, fraction · Vanilla(100))`, where `original(·, 0)` is the skill-free part `mult · base^exponent`.
  - It is installed with the target check against `26494`, as part of `InstallScaling`'s all-or-nothing set.

- [ ] **Step 1:** The math is already pinned by `TestEnchantChargeVanillaUpTo100`. Add a startup log of `ChargePerUse(100, ·)` at real skill 100 and at 200 when the player exists, which is useful in Task 9.
- [ ] **Step 2: Implement it and install it.**
- [ ] **Step 3: Build** — `scripts/build.sh`. Expected: green.
- [ ] **Step 4: Commit** — `feat: enchanting charge per use follows form B from the real skill (FR-30)`

---

### Task 6: Barter ratio floor (FR-26)

**Files:** Modify `src/Scaling.cpp`, `src/Scaling.h`, `src/Hooks.cpp`

**Interfaces:**
- Consumes: `BarterClampStub` (Task 3), `BarterFinalClamp` (Task 1), `kBarterFinalClamp` (Task 2).
- Produces: `void BarterClamp(float* a_buy, float* a_sell)`: `BarterFinalClamp(*a_buy, *a_sell, fMinBuyMult, fMaxSellMult, barterMinRatio)`, written back. It is installed by `write_call<5>` at the site plus NOPs over the remaining 59 bytes. It is part of `InstallScaling`.

- [ ] **Step 1:** The math is pinned by `TestBarterFinalClampFloorsRatio`; the stub by Task 3.
- [ ] **Step 2: Implement it and install it.**
- [ ] **Step 3: Build** — `scripts/build.sh`. Expected: green.
- [ ] **Step 4: Commit** — `feat: barter buy price stays at least 1.2x the sell price (FR-26)`

---

### Task 7: Character-XP clamp (FR-11)

**Files:** Modify `src/Hooks.cpp`, `PRD.md` (nothing changes unless a ruling arises)

**Interfaces:**
- Consumes: `CharXPClampStub` (Task 3), `kImproveLevelExpBySkillLevel`.
- Produces: `InstallCharXPClamp`. It is installed **only** when `bClampCharXPAbove100` is true. It byte-checks the site, emits the stub with `&GetSetting("fXPPerSkillRank")->data.f`, writes `write_call<5>` and 7 NOPs, and replaces the "not implemented yet" warning with an install line.

- [ ] **Step 1:** The stub is pinned by `TestCharXPClampStubPreservesRax` (Task 3).
- [ ] **Step 2: Implement it.**
- [ ] **Step 3: Build** — `scripts/build.sh`. Expected: green, and at the default setting the log says the hook is not installed.
- [ ] **Step 4: Commit** — `feat: optional character-XP clamp above 100 (FR-11)`

---

### Task 8: FR-10 warning (decision 5)

**Files:** Modify `src/Hooks.cpp` (`GetSkillCap`), `PRD.md` FR-10

**Interfaces:** `GetSkillCap` reads `skills->data->skills[i].levelThreshold` once per call (it runs on every XP gain). It logs one warning per skill per session when `Curves::ThresholdNeedsDouble` first holds.

- [ ] **Step 1: Implement it.** Edit FR-10 to say: "Below a threshold of 2²⁴ float accumulation is exact; above it the plugin logs a warning once per skill. A `double` accumulator is deferred until a reachable level needs it."
- [ ] **Step 2: Build** — `scripts/build.sh`. Expected: green.
- [ ] **Step 3: Commit** — `feat: warn once when a skill's XP threshold leaves float precision (FR-10)`

---

### Task 9: In-game verification and docs

**Files:** Modify `README.md` (Status), `docs/development.md` if anything changed in practice.

- [ ] **Step 1: Install the build** (game closed) and run Joel's checklist on a test save. Use console `player.setav <skill> <n>` with the cap raised so the value holds.

| Check | Expected |
|---|---|
| Log | `Scaling installed`, the self-check table, cached GMSTs = esm values (`fMagicCasterPCSkillCostBase` 0.0025, `fMagicPCSkillCostScale` 0.65, `fBarterMax` 3.3, `fBarterMin` 2.0), no `[W]`/`[E]` |
| FR-29 | at One-Handed 150 the skills menu shows **150** |
| Form A | weapon damage in the inventory: One-Handed 150 is above 100's damage and below a linear continuation |
| Spell cost | Destruction 100 vs 400: the cost factor drops by about 0.594 → 0.284 relative to the spell's base cost |
| Barter | Speech 1000 with all barter perks: every item buys for ≥ 1.2× its sell price |
| Enchanting | Enchanting 1000: a weapon enchantment still uses charge per hit |
| NPCs | an NPC's skill readings are unchanged (e.g. an enemy mage's spell costs, a merchant's prices for NPC trades) |
| Off switch | `bEnableFormulaScaling=false`: log says scaling not installed; the menu still works |

- [ ] **Step 2: Update README Status** with the results; commit `docs: record verified formula scaling`.
