// Native unit tests for src/Curves.h. Build and run on the host (no game needed):
//   c++ -std=c++20 -I src tests/test_curves.cpp -o build/test_curves && build/test_curves

#include "Curves.h"

#include <cmath>
#include <cstdio>

using namespace BM::Curves;

static int g_failures = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                    \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
			++g_failures;                                                 \
		}                                                                 \
	} while (0)

#define CHECK_NEAR(a, b, tol)                                                                         \
	do {                                                                                              \
		const double _a = (a), _b = (b);                                                              \
		if (std::abs(_a - _b) > (tol)) {                                                              \
			std::printf("FAIL %s:%d: %s = %.6f, expected %.6f\n", __FILE__, __LINE__, #a, _a, _b);   \
			++g_failures;                                                                             \
		}                                                                                             \
	} while (0)

#define CHECK_EQ(a, b)                                                                                \
	do {                                                                                              \
		const auto _a = (a), _b = (b);                                                                \
		if (!(_a == _b)) {                                                                            \
			std::printf("FAIL %s:%d: %s = %g, expected %g\n", __FILE__, __LINE__, #a,                 \
				static_cast<double>(_a), static_cast<double>(_b));                                    \
			++g_failures;                                                                             \
		}                                                                                             \
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
	const auto f = EnchantChargeFormula();
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

	CHECK(AtCap(100.0, 100));   // FR-2: a raise needs level == cap
	CHECK(AtCap(150.0, 100));   // above cap still counts as at cap
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
	CHECK(!ThresholdNeedsDouble(16777216.0));      // exactly 2^24 is still exact
	CHECK(ThresholdNeedsDouble(16777217.0));       // the first integer a float loses
	const double big = 1.7e7;
	CHECK(static_cast<float>(big) + 1.0f == static_cast<float>(big));  // float loses it
	CHECK(big + 1.0 != big);                                           // double keeps it
}

static void TestXPGainMultiplierAcrossLegendary()
{
	// Review Focus 4: after Legendary the level is 15 while the cap stays high.
	CHECK_EQ(XPGainMultiplier(15.0, 0.5), 1.0);   // below 100 -> vanilla
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

int main()
{
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
	if (g_failures == 0) {
		std::printf("All curve tests passed.\n");
	}
	return g_failures == 0 ? 0 : 1;
}
