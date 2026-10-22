# Legendary Upgrade Menu Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Pressing vanilla Legendary on a skill at 100+ opens a menu of Reset Skill / Upgrade / Cancel, and Upgrade raises the skill's limit by paying perks, filled black soul gems and recurring dragon-soul gates.

**Architecture:** Two call-site hooks replace the engine's `MessageBoxMenu::Create` call in the two `StatsMenu` functions that build the Legendary confirmation box (found by cross-referencing the callback's vtable). Our callback keeps the vanilla `LegendarySkillResetConfirmCallback` and calls its `Run(1)` for Reset Skill. Cap enforcement switches from the cap to the *limit*, `min(cap, 100 × (1 + gates paid this climb))`, and all cost and gate arithmetic is pure and natively tested in `Curves.h`.

**Tech Stack:** C++23, CommonLibSSE-NG (`trampoline.write_call<5>`, `MessageBoxMenu`, `IMessageBoxCallback`, NG runtime accessors), SKSE co-save, Python offline site verification.

**Spec:** [PRD.md](../../../PRD.md) v1.4 — FR-2, FR-3, FR-5 … FR-7, FR-12 … FR-17, FR-22, FR-31; §5 (Gate, Climb, Limit); §6.2 costs. Engine detail: [docs/hook-points.md](../../hook-points.md) §7.

## Global Constraints

- Runtimes: the menu installs on **AE 1.6.x and 1.7.x only**, and only when the cap hook installed — offering Upgrade where caps are not enforced would sell a raise that does nothing. SE 1.5.97 keeps vanilla Legendary (its sites are unverified, as for the cap hook).
- Hook addresses come from Address Library IDs only; every site is byte-signature checked before writing (CLAUDE.md "Working on hooks").
- Runtime-variant members only through NG accessors — `GetGameStatsData().perkCount`, never a fixed offset (CLAUDE.md; the layout shifts at 1.6.629 **and** 1.7.99).
- No change at skill ≤ 100 (PRD §3). Reset Skill must be indistinguishable from vanilla Legendary (FR-31, success criterion 11).
- Step cost: `1` perk point, `T − 1` filled black soul gems, `T = floor(C / 100)`. Gate *T*: `10 × T` dragon souls, once per climb (PRD §6.2).
- `limit = min(cap, 100 × (1 + gates paid this climb))` (PRD §5).
- Menu buttons, in order: **Reset Skill**, **Upgrade**, **Cancel**; Upgrade's box: **Confirm**, **Back** (FR-15, FR-16). Costs are shown only after Upgrade is chosen.
- No plugin file, no Papyrus, no change to the skill tree's Flash UI (FR-17).

## Engine facts this plan builds on

Established by disassembling SkyrimSE.exe 1.7.104 against its own `versionlib-1-7-104-0.bin`:

| Fact | Value |
|---|---|
| `LegendarySkillResetConfirmCallback` vtable | AE id `215997` → RVA `0x19803d8`; `Run` = slot 1 |
| `Run(button)` | resets only for `button == 1`, after a precondition call (`id 82074`) that can make it do nothing |
| Box creation site A | AE id `52510`; pattern at `+0x5E3`, the `call` 15 bytes in (`0x977042`) |
| Box creation site B | AE id `52520`; pattern at `+0x25C`, the `call` 15 bytes in (`0x9787eb`) |
| Both sites call | `MessageBoxMenu::Create(BSString& msg, const BSTSmartPointer<IMessageBoxCallback>& cb, std::uint8_t 0, std::int32_t 25, std::int32_t 10, const BSTArray<BSString>& buttons)` — id `AE_CHECK(1.6.1130, 52270, 442726)` |
| Pattern A | `45 33 C0 48 8D 95 28 01 00 00 48 8D 4C 24 58 E8 ?? ?? ?? ??` |
| Pattern B | `45 33 C0 48 8D 95 08 01 00 00 48 8D 4C 24 58 E8 ?? ?? ?? ??` |
| The Black Star | `0x00063B29` (`DA01SoulGemBlackStar`), NPC-capable, reusable |
| Black soul gems | `SoulGemBlack 0x0002E500` (empty; filled via `ExtraSoul`), `SoulGemBlackFilled 0x0002E504` (prefilled, grand) |

**Ruling (carried from planning):** the PRD's "black (humanoid) soul" is not observable on an item — `SOUL_LEVEL` has no black value, so a black gem holding a white grand soul looks identical. A *filled black soul gem* is therefore a `TESSoulGem` that `CanHoldNPCSoul()` and holds a **grand** soul, from its base form or `ExtraSoul`. Cost if wrong: a black gem filled with a white grand soul is accepted as payment.

## Review Focus

1. **Another mod already hooked a Legendary box call** (a Legendary overhaul). The call target no longer points at `MessageBoxMenu::Create`; we must skip that site and fold it into the single FR-20 warning, never chain onto an unknown function. → Task 5.
2. **Reset Skill chosen, but vanilla `Run(1)` declines** (its precondition fails). Gates must not be cleared when the level did not actually drop, or the player loses paid gates for nothing. → Task 5.
3. **An existing save without the new gate record.** Caps raised before this build must stay fully reachable: limit = cap, never a surprise lock at the next 100. → Task 3.
4. **Upgrade the player cannot afford.** Nothing is deducted; the box names what is missing and offers only Back (FR-5). → Task 4.
5. **The cap hook did not install** (SE, or a signature mismatch). The menu must not install either — Upgrade would raise an unenforced cap. → Task 5.

---

### Task 1: Put the two Legendary box sites in the site table and verify them offline

**Files:**
- Modify: `src/HookSites.h` (two sites, `kAllSites` 9 → 11)
- Modify: `tests/test_hooksites.cpp`, `tests/test_verify_sites.py`
- Modify: `docs/hook-points.md` §7.1 (confidence M → H, with the evidence table above)

**Interfaces:**
- Produces: `HookSites::kLegendaryBoxA`, `HookSites::kLegendaryBoxB` — `Site` literals, AE location as above, SE `{ 0, 0 }` with an empty SE pattern, `verified = true`; `HookSites::kLegendaryCallOffset = 15` (bytes from the pattern start to the `call`).

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/test_hooksites.cpp
static void TestLegendaryBoxSites()
{
	using namespace BM::HookSites;
	CHECK(kAllSites.size() == 11);
	CHECK(kLegendaryBoxA.ae.id == 52510 && kLegendaryBoxA.ae.offset == 0x5E3);
	CHECK(kLegendaryBoxB.ae.id == 52520 && kLegendaryBoxB.ae.offset == 0x25C);
	CHECK(kLegendaryCallOffset == 15);
	// The call opcode sits exactly kLegendaryCallOffset bytes into each pattern.
	for (const auto* s : { &kLegendaryBoxA, &kLegendaryBoxB })
		CHECK(s->patternAE.substr(kLegendaryCallOffset * 3, 2) == "E8");
}
```

```python
# tests/test_verify_sites.py — TestSiteTableParsing.test_parses_every_site_from_the_header
self.assertEqual(len(self.sites), 11)
```

`TestAgainstExactAddressLibrary.test_every_site_matches_at_zero_drift` then covers both new sites with no further change.

- [ ] **Step 2: Run to verify they fail**

Run: `scripts/test.sh`
Expected: FAIL — `kLegendaryBoxA` undeclared; Python reports 9 sites, expected 11.

- [ ] **Step 3: Add the two `Site` literals and `kLegendaryCallOffset` to `src/HookSites.h`**, with a comment recording how they were found (vtable cross-reference) and what the call is.

- [ ] **Step 4: Run to verify they pass**

Run: `scripts/test.sh`
Expected: PASS, including `test_every_site_matches_at_zero_drift` against the installed 1.7.104 database.

- [ ] **Step 5: Update hook-points.md §7.1 and commit**

```bash
git add src/HookSites.h tests/test_hooksites.cpp tests/test_verify_sites.py docs/hook-points.md
git commit -m "feat: locate and verify the two Legendary box creation sites"
```

---

### Task 2: Gate, limit and upgrade arithmetic

**Files:**
- Modify: `src/Curves.h`
- Test: `tests/test_curves.cpp`

**Interfaces:**
- Produces, all `noexcept`, in `BM::Curves`:
  - `std::uint16_t Limit(std::uint16_t a_cap, std::uint8_t a_gatesPaid)`
  - `std::uint8_t GatesNeededFor(std::uint16_t a_cap)` — gates a cap has passed: `ceil((cap − 100) / 100)`, 0 at cap 100
  - `enum class UpgradeKind : std::uint8_t { kGate, kStep, kStepAndGate };`
  - `struct Upgrade { UpgradeKind kind; StepCost cost; std::uint16_t newCap; std::uint8_t newGatesPaid; };`
  - `Upgrade UpgradeFor(std::uint16_t a_cap, std::uint8_t a_gatesPaid)` — precondition: the skill is at its limit
  - `struct GemPlan { std::uint32_t consumable; bool blackStar; };`
  - `std::optional<GemPlan> PlanGems(std::uint32_t a_needed, std::uint32_t a_consumable, bool a_blackStarFilled, bool a_useBlackStar)` — `nullopt` when the choice cannot cover `a_needed`
  - `bool CapRaisable(std::uint16_t a_cap)` — `a_cap + 10 <= kMaxPlausibleCap`. FR-4 sets no limit, but the co-save loader resets any cap above `kMaxPlausibleCap` to 100, so a raise past it would be lost on the next reload; Upgrade stops there instead.
- Consumes: `StepCost`, `CostOfNextStep`, `Tier` (existing).

- [ ] **Step 1: Write the failing tests**

```cpp
static void TestLimit()
{
	CHECK_EQ(Limit(100, 0), 100);
	CHECK_EQ(Limit(110, 1), 110);
	CHECK_EQ(Limit(250, 0), 100);  // after Reset Skill: every gate unpaid
	CHECK_EQ(Limit(250, 1), 200);
	CHECK_EQ(Limit(250, 2), 250);
	CHECK_EQ(Limit(250, 9), 250);  // excess gates never lift the limit past the cap
}

static void TestGatesNeededFor()
{
	// Migration default: a cap bought before gates recurred stays fully reachable.
	CHECK_EQ(GatesNeededFor(100), 0);
	CHECK_EQ(GatesNeededFor(110), 1);
	CHECK_EQ(GatesNeededFor(200), 1);
	CHECK_EQ(GatesNeededFor(210), 2);
	CHECK_EQ(GatesNeededFor(300), 2);
	for (std::uint16_t c = 100; c <= 1000; c += 10)
		CHECK_EQ(Limit(c, GatesNeededFor(c)), c);
}

static void TestUpgradeFor()
{
	// PRD success criterion 1: 100 -> 110 costs 1 perk + 10 dragon souls.
	auto u = UpgradeFor(100, 0);
	CHECK(u.kind == UpgradeKind::kStepAndGate);
	CHECK((u.cost == StepCost{ 1, 0, 10 }));
	CHECK_EQ(u.newCap, 110);
	CHECK_EQ(u.newGatesPaid, 1);
	// Mid-tier step: perk only.
	u = UpgradeFor(110, 1);
	CHECK(u.kind == UpgradeKind::kStep);
	CHECK((u.cost == StepCost{ 1, 0, 0 }));
	// PRD success criterion 2: 200 -> 210 costs 1 perk, 1 gem, 20 souls.
	u = UpgradeFor(200, 1);
	CHECK(u.kind == UpgradeKind::kStepAndGate);
	CHECK((u.cost == StepCost{ 1, 1, 20 }));
	CHECK_EQ(u.newGatesPaid, 2);
	// Re-climb after Reset Skill at cap 250: gates only, cap unchanged (PRD §6.2).
	u = UpgradeFor(250, 0);
	CHECK(u.kind == UpgradeKind::kGate);
	CHECK((u.cost == StepCost{ 0, 0, 10 }));
	CHECK_EQ(u.newCap, 250);
	CHECK_EQ(u.newGatesPaid, 1);
	u = UpgradeFor(250, 1);
	CHECK((u.cost == StepCost{ 0, 0, 20 }));
	// Re-climb finished: the next upgrade is an ordinary step again.
	u = UpgradeFor(250, 2);
	CHECK(u.kind == UpgradeKind::kStep);
	CHECK((u.cost == StepCost{ 1, 1, 0 }));
	CHECK_EQ(u.newCap, 260);
}

static void TestPlanGems()
{
	// FR-6/FR-7: The Black Star counts as one gem and is the player's choice.
	CHECK(!PlanGems(2, 1, false, false));                        // short
	CHECK((*PlanGems(2, 2, false, false)).consumable == 2);
	auto p = *PlanGems(2, 1, true, true);                        // star + one consumable
	CHECK(p.blackStar && p.consumable == 1);
	p = *PlanGems(2, 5, true, false);                            // keep the star
	CHECK(!p.blackStar && p.consumable == 2);
	CHECK(!PlanGems(2, 0, true, true));                          // star alone covers one
	CHECK((*PlanGems(0, 0, false, false)).consumable == 0);      // tier 1: no gems
}

static void TestCapRaisable()
{
	// A cap the co-save loader would reject must never be created.
	CHECK(CapRaisable(100));
	CHECK(CapRaisable(kMaxPlausibleCap - 10));
	CHECK(!CapRaisable(kMaxPlausibleCap));
	CHECK(IsPlausibleCap(kMaxPlausibleCap));  // the last raise still loads
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `scripts/test.sh`
Expected: FAIL — `Limit`, `GatesNeededFor`, `UpgradeFor`, `PlanGems`, `CapRaisable` undeclared.

- [ ] **Step 3: Implement the six functions in `src/Curves.h`**

`UpgradeFor`: if `Limit(cap, gates) < cap`, it is gate `T = Limit / 100` → `kGate`, souls `10 × T`, `newGatesPaid = gates + 1`. Otherwise it is the step from `cap` (`CostOfNextStep` for perks and gems); add the gate's souls and `newGatesPaid = cap / 100` only when `cap % 100 == 0` and `gates < cap / 100`, else leave souls at 0.

- [ ] **Step 4: Run to verify they pass**

Run: `scripts/test.sh`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/Curves.h tests/test_curves.cpp
git commit -m "feat: limit, recurring-gate and upgrade arithmetic (PRD v1.4)"
```

---

### Task 3: Per-skill gates in the co-save, and enforce the limit

**Files:**
- Modify: `src/State.h`, `src/State.cpp`
- Modify: `src/Hooks.cpp` (`GetSkillCap` returns the limit)

**Interfaces:**
- Produces on `BM::State`:
  - `std::uint8_t GatesPaid(std::size_t a_skill) const noexcept`, `void SetGatesPaid(std::size_t, std::uint8_t) noexcept`
  - `std::uint16_t LimitOf(RE::ActorValue) const noexcept` — `Curves::Limit` for skills, `kDefaultCap` otherwise
  - `void ApplyUpgrade(std::size_t a_skill, const Curves::Upgrade&) noexcept`
  - `void ClearGates(std::size_t a_skill) noexcept`
- Co-save: new record `'GATE'` version 1 = `std::array<std::uint8_t, Skills::kCount>`. `'CAPS'` v1 is unchanged, so saves written before this build load as they are.
- Consumes: Task 2's `Limit`, `GatesNeededFor`.

- [ ] **Step 1: Store gates and write `'GATE'` in `OnSave`**

- [ ] **Step 2: Load `'GATE'` in `OnLoad`, with Review Focus 3's default**

When no `'GATE'` record is present, set each skill's gates to `GatesNeededFor(cap)`, so a cap from an older save stays fully reachable. Clamp every loaded value to `GatesNeededFor(cap)`: more gates than the cap has passed can never be needed, and a corrupt value is then harmless. `OnRevert` and `Reset` zero all gates.

- [ ] **Step 3: Switch the cap hook to the limit**

`Hooks::GetSkillCap` returns `State::Get().LimitOf(a_av)`. At default state the limit is 100 for every skill, so vanilla parity holds — `TestVanillaParityAtDefaults` plus `CHECK_EQ(Limit(100, 0), 100)` pin it.

- [ ] **Step 4: Build and test**

Run: `scripts/build.sh`
Expected: `BUILD_EXIT=0`, all tests pass, `OK ... no C++ runtime imports`.
The co-save and the hook are game-side, so this proves they compile and the
arithmetic they call is tested; their behaviour is verified in-game in Task 6.

- [ ] **Step 5: Commit**

```bash
git add src/State.h src/State.cpp src/Hooks.cpp
git commit -m "feat: track paid gates per skill and enforce the limit (FR-13)"
```

---

### Task 4: Read, check and spend the player's resources

**Files:**
- Create: `src/Resources.h`, `src/Resources.cpp`
- Modify: `CMakeLists.txt` (sources)

**Interfaces:**
- Produces in `BM::Resources`:
  - `struct Holdings { std::uint32_t perks; std::uint32_t dragonSouls; std::uint32_t consumableGems; bool blackStarFilled; };`
  - `Holdings Read()` — perks from `GetGameStatsData().perkCount`, souls from `ActorValue::kDragonSouls`, gems by inventory scan under the ruling above (The Black Star = form `0x00063B29`)
  - `std::uint32_t GemsAvailable(const Holdings&) noexcept` — `consumableGems + (blackStarFilled ? 1 : 0)`
  - `bool Spend(const Curves::StepCost&, const Curves::GemPlan&)` — deducts; returns false and spends nothing if a re-read just before spending no longer covers the cost
- Consumes: `Curves::StepCost`, `Curves::Affordable`, `Curves::GemPlan`.

- [ ] **Step 1: Implement `Read` and `GemsAvailable`**

Consumable gems: `SoulGemBlackFilled` stacks, plus `SoulGemBlack` entries whose `ExtraSoul` is grand. The Black Star is filled when its `ExtraSoul` is grand.

- [ ] **Step 2: Implement `Spend`, re-checking before the first deduction (FR-5)**

Perks: `GetGameStatsData().perkCount -= cost.perks`. Souls: `AsActorValueOwner()->ModActorValue(ActorValue::kDragonSouls, -souls)`. Consumable gems: `RemoveItem` on the filled stacks. The Black Star: remove the filled entry and add one empty Black Star, so its soul is gone and the item kept (FR-6).

- [ ] **Step 3: Build and test**

Run: `scripts/build.sh`
Expected: `BUILD_EXIT=0`, all tests pass.
Reading and spending touch the player and the inventory, so nothing here runs
natively; `Affordable` and `PlanGems` (Task 2) carry the logic, and Task 6
verifies the spending in-game, including the gem path.

- [ ] **Step 4: Commit**

```bash
git add src/Resources.h src/Resources.cpp CMakeLists.txt
git commit -m "feat: read and spend perks, dragon souls and black soul gems (FR-5..7)"
```

---

### Task 5: The Legendary menu and its hooks

**Files:**
- Create: `src/Menu.h`, `src/Menu.cpp`
- Modify: `src/Hooks.cpp` (install both call-site hooks), `src/Hooks.h`, `CMakeLists.txt`
- Modify: `tests/test_stubs.cpp` (trampoline budget: two more 14-byte thunks)

**Interfaces:**
- Produces:
  - `bool Menu::OpenLegendary(RE::BSString&, const RE::BSTSmartPointer<RE::IMessageBoxCallback>& a_vanilla, std::uint8_t, std::int32_t, std::int32_t, const RE::BSTArray<RE::BSString>&)` — the call-site thunk; same signature as `MessageBoxMenu::Create`
  - `bool Hooks::LegendaryMenuInstalled() noexcept`
- Consumes: Tasks 1–4.

- [ ] **Step 1: Write the budget test change, then run it**

`TestStubsFitTheTrampolineBudget` asserts `cap + xp + 4 * 14 < 256` (the two new call sites add a thunk each).
Run: `scripts/test.sh` — Expected: PASS (it is a budget check, not new behaviour).

- [ ] **Step 2: Implement the three boxes in `src/Menu.cpp`**

| Box | When | Buttons | Body |
|---|---|---|---|
| Main | level ≥ limit, and not (limit = cap and `!CapRaisable(cap)`) | Reset Skill, Upgrade, Cancel | skill name |
| Main | level < limit | Reset Skill, Cancel | "Reach *limit* to upgrade *skill*." |
| Upgrade | Upgrade chosen | Confirm, Back — or Back only when unaffordable | what the upgrade does (new cap, or the gate passed), each cost line with the player's holding, and any shortfall named (FR-5, FR-16) |
| Black Star | Confirm chosen, and `PlanGems` succeeds both with and without the star | Use The Black Star, Keep The Black Star, Back | — |

Each box is a `MessageBoxMenu::Create` with our own `IMessageBoxCallback`, holding the skill and a `BSTSmartPointer` to the vanilla callback. Back reopens the main box. Confirm runs `Resources::Spend`, then `State::ApplyUpgrade`. Every button indexes into the buttons that box actually shows.

- [ ] **Step 3: Implement Reset Skill with Review Focus 2's guard**

Record the skill's base level, call `vanilla->Run(1)`, and only if the base level then dropped call `State::ClearGates`. Log either outcome at info level.

- [ ] **Step 4: Install the two call-site hooks with Review Focus 1's guard**

For each site: check the pattern; decode the existing `call`'s target and require it to equal `MessageBoxMenu::Create`'s address (`RELOCATION_ID(51421, AE_CHECK(SKSE::RUNTIME_SSE_1_6_1130, 52270, 442726))`); only then `write_call<5>` at site + `kLegendaryCallOffset`. A wrong target is a signature mismatch: skip, and fold it into the FR-20 warning. Install only when `CapHookInstalled()` is true (Review Focus 5). `SiteIsNeeded` returns that same condition for both sites.

- [ ] **Step 5: Build and test**

Run: `scripts/build.sh`
Expected: `BUILD_EXIT=0`, all tests pass, runtime gate OK.

- [ ] **Step 6: Commit**

```bash
git add src/Menu.h src/Menu.cpp src/Hooks.cpp src/Hooks.h CMakeLists.txt tests/test_stubs.cpp
git commit -m "feat: Legendary menu with Reset Skill and Upgrade (FR-14..17, FR-31)"
```

---

### Task 6: In-game verification and docs

**Files:**
- Modify: `README.md`, `CLAUDE.md` (milestone), `docs/hook-points.md` §7

- [ ] **Step 1: Install and verify in-game on a throwaway save**

| PRD | Do | Expect |
|---|---|---|
| criterion 1 | `player.setav onehanded 100`, give 1 perk + 10 dragon souls, Legendary → Upgrade → Confirm | cap 110; One-Handed then trains to 110 and stops |
| criterion 2 | cap 200 at level 200, 1 perk, 20 souls, one `SoulGemBlackFilled` | Upgrade consumes exactly 1 perk, 1 gem, 20 souls; cap 210 |
| FR-6/FR-7 | as above, but also a filled Black Star | Black Star box appears; "Use" empties the Star and keeps it, "Keep" spends the consumable |
| criterion 11 | Legendary → Reset Skill | exactly vanilla: level 15, perks refunded |
| criterion 3 | with cap 250, Reset Skill, retrain | stops at 100 and at 200 until Upgrade pays 10, then 20 souls |
| FR-5 | Upgrade with too few dragon souls | shortfall named, only Back, nothing spent |
| FR-15 | Legendary at level 105 with limit 110 | "Reach 110…", Reset Skill and Cancel only |
| FR-22 | save, reload | caps and gates restored |

`BeyondMastery.log` shows both Legendary sites `kReady` and the menu installed.

- [ ] **Step 2: Update README status, CLAUDE.md milestone and hook-points §7 to what was verified, and commit**

```bash
git add README.md CLAUDE.md docs/hook-points.md
git commit -m "docs: record the Legendary menu's in-game verification"
```
