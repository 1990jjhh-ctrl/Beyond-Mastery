// Native unit tests for src/Curves.h. Build and run on the host (no game needed):
//   c++ -std=c++20 -I src tests/test_curves.cpp -o build/test_curves && build/test_curves

#include "Curves.h"

#include <cmath>
#include <cstdio>
#include <limits>

using namespace BM::Curves;

static int g_failures = 0;

#define CHECK(cond)                                                     \
	do {                                                                \
		if (!(cond)) {                                                  \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			++g_failures;                                               \
		}                                                               \
	} while (0)

#define CHECK_NEAR(a, b, tol)                                                                      \
	do {                                                                                           \
		const double _a = (a), _b = (b);                                                           \
		if (std::abs(_a - _b) > (tol)) {                                                           \
			std::printf("FAIL %s:%d: %s = %.6f, expected %.6f\n", __FILE__, __LINE__, #a, _a, _b); \
			++g_failures;                                                                          \
		}                                                                                          \
	} while (0)

#define CHECK_EQ(a, b)                                                                \
	do {                                                                              \
		const auto _a = (a);                                                          \
		const auto _b = (b);                                                          \
		if (!(_a == _b)) {                                                            \
			std::printf("FAIL %s:%d: %s = %g, expected %g\n", __FILE__, __LINE__, #a, \
				static_cast<double>(_a), static_cast<double>(_b));                    \
			++g_failures;                                                             \
		}                                                                             \
	} while (0)

static void TestCosts()
{
	// PRD §6.2 example table (v1.2: one perk point per step, whatever the step).
	CHECK((CostOfNextStep(100) == StepCost{ 1, 0, 10 }));
	CHECK((CostOfNextStep(110) == StepCost{ 1, 0, 0 }));
	CHECK((CostOfNextStep(190) == StepCost{ 1, 0, 0 }));
	CHECK((CostOfNextStep(200) == StepCost{ 1, 1, 20 }));
	CHECK((CostOfNextStep(290) == StepCost{ 1, 1, 0 }));
	CHECK((CostOfNextStep(300) == StepCost{ 1, 2, 30 }));

	// The perk cost must not grow with the step number, however deep.
	CHECK_EQ(CostOfNextStep(100 + 10 * 300).perks, 1u);

	auto total = [](std::uint32_t from, std::uint32_t to) {
		StepCost t;
		for (auto c = from; c < to; c += 10) {
			const auto s = CostOfNextStep(c);
			t.perks += s.perks;
			t.blackSoulGems += s.blackSoulGems;
			t.dragonSouls += s.dragonSouls;
		}
		return t;
	};
	CHECK((total(100, 200) == StepCost{ 10, 0, 10 }));
	CHECK((total(200, 300) == StepCost{ 10, 10, 20 }));
	CHECK((total(300, 400) == StepCost{ 10, 20, 30 }));
}

static void TestXPGain()
{
	CHECK_NEAR(XPGainMultiplier(50, 0.5), 1.0, 1e-12);
	CHECK_NEAR(XPGainMultiplier(150, 0.5), 1.0, 1e-12);
	CHECK_NEAR(XPGainMultiplier(200, 0.5), 0.5, 1e-12);
	CHECK_NEAR(XPGainMultiplier(350, 0.5), 0.25, 1e-12);
	CHECK_NEAR(XPGainMultiplier(350, 2.0), 1.0, 1e-12);  // clamped to <= 1
}

static void TestFormA()
{
	CHECK_NEAR(SqrtEffectiveSkill(50), 50, 1e-12);
	CHECK_NEAR(SqrtEffectiveSkill(100), 100, 1e-12);
	// slope 1 at 100
	CHECK_NEAR((SqrtEffectiveSkill(100.001) - 100.0) / 0.001, 1.0, 1e-3);
	CHECK_NEAR(SqrtEffectiveSkill(200), 182.84, 0.01);
	CHECK_NEAR(SqrtEffectiveSkill(400), 300.0, 1e-9);
	// Enchanting charge formula breaks at 200: reached at s ~ 225
	CHECK(SqrtEffectiveSkill(224) < 199.5);
	CHECK(SqrtEffectiveSkill(226) > 200.0);
}

static void TestSpellCost()
{
	const SpellCostFormula f;
	CHECK_NEAR(f.Vanilla(100), 0.5939, 1e-4);
	const auto m = f.MakeAsymptote(0.25);
	CHECK_NEAR(m.Tau(), 130.3, 0.5);
	// PRD success criterion 8
	CHECK_NEAR(m(400), 0.29, 0.01);
	CHECK(m(1e6) >= 0.25);
	// slope continuity at 100
	CHECK_NEAR((m(100.001) - f.Vanilla(100)) / 0.001, f.VanillaSlope(100), 1e-5);

	// Inverse: engine computing vanilla(s_eff) must yield m(s)
	for (double s : { 101.0, 150.0, 200.0, 400.0, 2000.0 }) {
		CHECK_NEAR(f.Vanilla(f.EffectiveSkill(s, 0.25)), m(s), 1e-9);
	}
	CHECK_NEAR(f.EffectiveSkill(100, 0.25), 100.0, 1e-9);
	CHECK(f.EffectiveSkill(1e9, 0.25) < 257.5);
}

static void TestEnchantCharge()
{
	const auto   f = EnchantChargeFormula();
	const double at100 = f.Vanilla(100);
	CHECK_NEAR(at100, 0.29289, 1e-5);
	CHECK_NEAR(f.Vanilla(200), 0.0, 1e-12);  // vanilla breaks here

	const double limit = at100 / 2.0;  // FR-30: half the value at 100
	const auto   a = f.MakeAsymptote(limit);
	CHECK_NEAR(a.Tau(), 41.42, 0.01);
	CHECK_NEAR(f.Factor(100, limit), at100, 1e-12);
	CHECK_NEAR((f.Factor(100.001, limit) - at100) / 0.001, f.VanillaSlope(100), 1e-5);
	CHECK_NEAR(f.Factor(200, limit), 0.1595, 1e-3);
	CHECK(f.Factor(1e6, limit) >= limit);
	CHECK(f.Factor(1e6, limit) < limit + 1e-9);
}

static void TestBarter()
{
	const BarterFormula b;
	CHECK_NEAR(b.Vanilla(100), 2.0, 1e-12);
	const auto a = b.MakeAsymptote(1.2);
	CHECK_NEAR(a.Tau(), 61.54, 0.01);
	CHECK(a(1e6) >= 1.2);
	CHECK_NEAR(ClampBuyPrice(100, 90, 1.2), 108, 1e-12);
	CHECK_NEAR(ClampBuyPrice(200, 90, 1.2), 200, 1e-12);
}

static void TestCapClamp()
{
	// Review Focus 2: a level above its cap must clamp, never go negative or wrap.
	CHECK_EQ(ClampLevelToCap(150.0, 100), 100.0);
	CHECK_EQ(ClampLevelToCap(100.0, 100), 100.0);
	CHECK_EQ(ClampLevelToCap(42.0, 100), 42.0);
	CHECK_EQ(ClampLevelToCap(305.0, 300), 300.0);
	CHECK_EQ(ClampLevelToCap(0.0, 100), 0.0);
	CHECK_EQ(ClampLevelToCap(-5.0, 100), -5.0);  // engine's problem, not ours to invent

	CHECK(AtCap(100.0, 100));  // FR-2: a raise needs level == cap
	CHECK(AtCap(150.0, 100));  // above cap still counts as at cap
	CHECK(!AtCap(99.0, 100));
	CHECK(AtCap(300.0, 300));
	CHECK(!AtCap(299.0, 300));
}

static void TestVanillaParityAtDefaults()
{
	// Review Focus 3: at defaults every hooked value must equal vanilla exactly.
	CHECK_EQ(ClampLevelToCap(100.0, 100), 100.0);
	CHECK_EQ(XPGainMultiplier(100.0, 1.0), 1.0);
	CHECK_EQ(XPGainMultiplier(400.0, 1.0), 1.0);  // f = 1.0 is vanilla at every level
	CHECK_EQ(SqrtEffectiveSkill(100.0), 100.0);
}

static void TestXPThreshold()
{
	// FR-9: ImproveMult * level^fSkillUseCurve + ImproveOffset, extended past 100.
	CHECK_NEAR(XPThreshold(100, 1.0, 1.95, 0.0), std::pow(100.0, 1.95), 1e-6);
	CHECK_NEAR(XPThreshold(100, 2.0, 1.95, 5.0), 2.0 * std::pow(100.0, 1.95) + 5.0, 1e-6);
	CHECK(XPThreshold(200, 1.0, 1.95, 0.0) > XPThreshold(100, 1.0, 1.95, 0.0));

	// FR-10: past 2^24 a float can no longer hold consecutive integers, so the
	// plugin has to track progress as double from there on.
	CHECK(!ThresholdNeedsDouble(1.0e6));
	CHECK(ThresholdNeedsDouble(1.7e7));
	CHECK(!ThresholdNeedsDouble(16777216.0));  // exactly 2^24 is still exact
	CHECK(ThresholdNeedsDouble(16777217.0));   // the first integer a float loses
	const double big = 1.7e7;
	CHECK(static_cast<float>(big) + 1.0f == static_cast<float>(big));  // float loses it
	CHECK(big + 1.0 != big);                                           // double keeps it
}

static void TestXPGainMultiplierAcrossLegendary()
{
	// Review Focus 4: after Legendary the level is 15 while the cap stays high.
	CHECK_EQ(XPGainMultiplier(15.0, 0.5), 1.0);  // below 100 -> vanilla
	CHECK_EQ(XPGainMultiplier(99.0, 0.5), 1.0);
	CHECK_EQ(XPGainMultiplier(100.0, 0.5), 1.0);  // T=1 -> f^0
	CHECK_NEAR(XPGainMultiplier(200.0, 0.5), 0.5, 1e-12);
	CHECK_NEAR(XPGainMultiplier(300.0, 0.5), 0.25, 1e-12);
	CHECK_EQ(XPGainMultiplier(300.0, 1.0), 1.0);  // default f = vanilla everywhere
}

static void TestFlatPerkCostAndAffordability()
{
	// PRD v1.2 §6.2: one perk point per step, whatever the step number.
	CHECK_EQ(CostOfNextStep(100).perks, 1u);
	CHECK_EQ(CostOfNextStep(200).perks, 1u);
	CHECK_EQ(CostOfNextStep(100 + 10 * 300).perks, 1u);
	// FR-5: every resource is checked before anything is deducted.
	CHECK(Affordable(CostOfNextStep(200), 1, 1, 20));
	CHECK(!Affordable(CostOfNextStep(200), 0, 1, 20));  // no perk point
	CHECK(!Affordable(CostOfNextStep(200), 1, 0, 20));  // no black soul gem
	CHECK(!Affordable(CostOfNextStep(200), 1, 1, 19));  // one dragon soul short
	CHECK(Affordable(CostOfNextStep(200), 9, 9, 99));   // surplus is fine
	CHECK(Affordable(CostOfNextStep(110), 1, 0, 0));    // mid-tier step: perk only
}

static void TestPlausibleCap()
{
	// A cap read from a co-save is data we did not write, and it now feeds the
	// engine's level comparison directly, so implausible values must be
	// rejected rather than enforced.
	CHECK(IsPlausibleCap(100));
	CHECK(IsPlausibleCap(110));
	CHECK(IsPlausibleCap(1000));
	CHECK(!IsPlausibleCap(0));      // empty / zeroed record
	CHECK(!IsPlausibleCap(90));     // below the vanilla cap
	CHECK(!IsPlausibleCap(105));    // not a step boundary
	CHECK(!IsPlausibleCap(65530));  // garbage that is still a multiple of 10
}

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
	CHECK(!PlanGems(2, 1, false, false));  // short
	CHECK((*PlanGems(2, 2, false, false)).consumable == 2);
	auto p = *PlanGems(2, 1, true, true);  // star + one consumable
	CHECK(p.blackStar && p.consumable == 1);
	p = *PlanGems(2, 5, true, false);  // keep the star
	CHECK(!p.blackStar && p.consumable == 2);
	CHECK(!PlanGems(2, 0, true, true));                      // star alone covers one
	CHECK((*PlanGems(0, 0, false, false)).consumable == 0);  // tier 1: no gems
}

static void TestCapRaisable()
{
	// A cap the co-save loader would reject must never be created.
	CHECK(CapRaisable(100));
	CHECK(CapRaisable(kMaxPlausibleCap - 10));
	CHECK(!CapRaisable(kMaxPlausibleCap));
	CHECK(IsPlausibleCap(kMaxPlausibleCap));  // the last raise still loads
}

static void TestDragonSoulsPaid()
{
	// What Reset Skill forfeits: the gates paid this climb, 10 x T each (PRD §6.2).
	CHECK_EQ(DragonSoulsPaid(0), 0u);
	CHECK_EQ(DragonSoulsPaid(1), 10u);
	CHECK_EQ(DragonSoulsPaid(2), 30u);  // the 250 re-climb: 10 + 20
	CHECK_EQ(DragonSoulsPaid(3), 60u);
	// Agrees with UpgradeFor, gate by gate.
	std::uint32_t sum = 0;
	for (std::uint8_t g = 0; g < 9; ++g) {
		sum += UpgradeFor(1000, g).cost.dragonSouls;
		CHECK_EQ(DragonSoulsPaid(static_cast<std::uint8_t>(g + 1)), sum);
	}
}

// Formula scaling (FR-24 ... FR-30): the remaps, the value clamp and the barter floor.
static void TestBarterEffectiveSkill()  // fBarterMax 3.3, fBarterMin 2.0, limit sqrt(1.2)
{
	const BM::Curves::BarterFormula b{ 3.3, 2.0 };
	const double                    L = std::sqrt(1.2);
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
	CHECK_NEAR(p.buy, 1.14, 1e-9);
	CHECK_NEAR(p.sell, 0.95, 1e-9);
	p = BM::Curves::BarterFinalClamp(0.8, 0.9, 1.0, 1.0, 1.2);  // vanilla floor, then the ratio
	CHECK_NEAR(p.buy, 1.08, 1e-9);
	CHECK_NEAR(p.sell, 0.9, 1e-9);
	p = BM::Curves::BarterFinalClamp(2.0, 0.5, 1.0, 1.0, 1.2);  // already fine: unchanged
	CHECK_NEAR(p.buy, 2.0, 1e-9);
	CHECK_NEAR(p.sell, 0.5, 1e-9);
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
	const auto                         engine = [&](double s) { return f.Vanilla(std::floor(f.EffectiveSkill(s, 0.25))); };
	CHECK_NEAR(engine(100.0), f.Vanilla(100.0), 1e-12);
	CHECK_NEAR(engine(400.0), 0.284, 0.01);  // criterion 8: 0.29 +- 0.01
	for (double s = 100.0; s <= 1e5; s *= 1.5) CHECK(engine(s) >= 0.25);
}
static void TestEnchantChargeVanillaUpTo100()  // base 0.005, scale 0.5, limit 0.5 x value at 100
{
	const auto   e = BM::Curves::EnchantChargeFormula(0.005, 0.5);
	const double L = 0.5 * e.Vanilla(100.0);
	for (double s : { 15.0, 50.0, 99.0, 100.0 }) CHECK_NEAR(e.Factor(s, L), e.Vanilla(s), 1e-12);
	CHECK_NEAR(e.Factor(125.0, L), 0.227, 0.001);
	CHECK_NEAR(e.Factor(150.0, L), 0.190, 0.001);
	CHECK_NEAR(e.Factor(200.0, L), 0.160, 0.001);
	CHECK(e.Factor(1000.0, L) >= 0.146);  // criterion 10
}
static void TestRemapsFiniteAtConfigExtremes()
{
	const BM::Curves::SpellCostFormula f{ 0.0025, 0.65 };
	const BM::Curves::BarterFormula    b{ 3.3, 2.0 };
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

static void TestRemapNeverBelow100WhenLimitIsNotBelowValueAt100()
{
	// A limit on the wrong side of the value at 100 (config or a GMST mod)
	// must not make the skill count for less than 100 or run backwards.
	const BM::Curves::BarterFormula    vanillaBarter{ 3.3, 2.0 };
	const BM::Curves::BarterFormula    moddedBarter{ 3.3, 1.0 };    // a mod lowering fBarterMin
	const BM::Curves::SpellCostFormula moddedSpell{ 0.003, 0.65 };  // Vanilla(100) = 0.543
	const auto                         check = [](auto a_eff) {
		double prev = 100.0;
		for (double s = 100.0; s <= 1e5; s *= 1.3) {
			const double e = a_eff(s);
			CHECK(std::isfinite(e) && e >= 100.0 && e >= prev - 1e-9);
			prev = e;
		}
	};
	check([&](double s) { return vanillaBarter.EffectiveSkill(s, std::sqrt(5.0)); });
	check([&](double s) { return moddedBarter.EffectiveSkill(s, std::sqrt(1.2)); });
	check([&](double s) { return moddedSpell.EffectiveSkill(s, 0.59); });
}

static void TestClampTreatsNonFiniteLikeTheEngine()
{
	// The engine clamps with minss/maxss, which turn NaN and infinity into the
	// upper bound. Non-finite values get exactly that, for the player too:
	// lifting the ceiling only applies to real numbers.
	// Exact equality, not CHECK_NEAR: |NaN - x| > tol is false, so a NaN
	// result would slip through a tolerance check.
	using namespace BM::Curves;
	const double nan = std::numeric_limits<double>::quiet_NaN();
	const double inf = std::numeric_limits<double>::infinity();
	CHECK(ClampActorValue(nan, kClampAsSkill, false) == 100.0);
	CHECK(ClampActorValue(nan, kClampAsSkill, true) == 100.0);
	CHECK(ClampActorValue(inf, kClampAsSkill, true) == 100.0);
	CHECK(ClampActorValue(-inf, kClampAsSkill, true) == 0.0);
	CHECK(ClampActorValue(nan, kClampAsSpecialStat, false) == 10.0);
	CHECK(ClampActorValue(nan, kClampAsSpecialStat | kSpecialStatClampsAsNonZero, false) == 10.0);
}

int main()
{
	TestDragonSoulsPaid();
	TestLimit();
	TestGatesNeededFor();
	TestUpgradeFor();
	TestPlanGems();
	TestCapRaisable();
	TestCosts();
	TestPlausibleCap();
	TestXPThreshold();
	TestXPGainMultiplierAcrossLegendary();
	TestFlatPerkCostAndAffordability();
	TestCapClamp();
	TestVanillaParityAtDefaults();
	TestXPGain();
	TestFormA();
	TestSpellCost();
	TestEnchantCharge();
	TestBarter();
	TestBarterEffectiveSkill();
	TestBarterFinalClampFloorsRatio();
	TestClampMatchesVanillaForNPCs();
	TestSpellCostThroughIntTruncation();
	TestEnchantChargeVanillaUpTo100();
	TestRemapsFiniteAtConfigExtremes();
	TestRemapNeverBelow100WhenLimitIsNotBelowValueAt100();
	TestClampTreatsNonFiniteLikeTheEngine();
	if (g_failures == 0) {
		std::printf("All curve tests passed.\n");
	}
	return g_failures == 0 ? 0 : 1;
}
