#pragma once

// Pure math for Beyond Mastery. No game or CommonLib dependencies, so this
// header is unit-tested natively on Linux (tests/test_curves.cpp).
//
// PRD references: §6.2 (costs), FR-9 (XP gain), §6.9 (formula scaling).

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace BM::Curves
{
	inline constexpr double kVanillaCap = 100.0;
	inline constexpr double kCapStep = 10.0;

	// ---------------------------------------------------------------------
	// Tiers and cap-raise costs (PRD §6.2)
	// ---------------------------------------------------------------------

	// Tier of a cap or skill level: floor(x / 100). 100-199 -> 1, 200-299 -> 2.
	[[nodiscard]] inline std::uint32_t Tier(double a_value) noexcept
	{
		return a_value < 0.0 ? 0u : static_cast<std::uint32_t>(a_value / 100.0);
	}

	// Step number n of raising the cap from a_currentCap to a_currentCap + 10.
	// Step 1 = 100 -> 110. Counted continuously across tiers.
	[[nodiscard]] inline std::uint32_t StepNumber(std::uint32_t a_currentCap) noexcept
	{
		return a_currentCap < 100u ? 0u : (a_currentCap - 100u) / 10u + 1u;
	}

	struct StepCost
	{
		std::uint32_t perks{ 0 };
		std::uint32_t blackSoulGems{ 0 };
		std::uint32_t dragonSouls{ 0 };

		friend bool operator==(const StepCost&, const StepCost&) = default;
	};

	// Cost of raising the cap from a_currentCap (a multiple of 10, >= 100) by +10.
	[[nodiscard]] inline StepCost CostOfNextStep(std::uint32_t a_currentCap) noexcept
	{
		const auto n = StepNumber(a_currentCap);
		const auto t = Tier(static_cast<double>(a_currentCap));
		StepCost cost;
		cost.perks = n;
		cost.blackSoulGems = t >= 1u ? t - 1u : 0u;
		cost.dragonSouls = (a_currentCap % 100u == 0u) ? 10u * t : 0u;
		return cost;
	}

	// ---------------------------------------------------------------------
	// Skill XP gain above 100 (FR-9)
	// ---------------------------------------------------------------------

	// Multiplier on XP gained at base skill level a_level: f^(T-1) for level >= 100,
	// 1 below. f is clamped to (0, 1].
	[[nodiscard]] inline double XPGainMultiplier(double a_level, double a_factorPerTier) noexcept
	{
		const double f = std::clamp(a_factorPerTier, 1e-6, 1.0);
		const auto   t = Tier(a_level);
		if (t <= 1u) {
			return 1.0;
		}
		return std::pow(f, static_cast<double>(t - 1u));
	}

	// ---------------------------------------------------------------------
	// Form A: sub-linear effective skill (PRD §6.9 A)
	// ---------------------------------------------------------------------

	// s_eff(s) = 100 + 200 * (sqrt(s/100) - 1) for s > 100, identity otherwise.
	// C1-continuous at 100: s_eff(100) = 100, s_eff'(100) = 1.
	[[nodiscard]] inline double SqrtEffectiveSkill(double a_skill) noexcept
	{
		if (a_skill <= kVanillaCap) {
			return a_skill;
		}
		return kVanillaCap + 2.0 * kVanillaCap * (std::sqrt(a_skill / kVanillaCap) - 1.0);
	}

	// ---------------------------------------------------------------------
	// Form B: asymptotic continuation (PRD §6.9 B)
	// ---------------------------------------------------------------------

	// f(s) = L + (f100 - L) * exp(-(s - 100) / tau), tau = (f100 - L) / |f'100|.
	// Matches value and slope of the vanilla curve at s = 100.
	struct Asymptote
	{
		double limit{ 0.0 };     // L
		double valueAt100{ 0.0 };  // f(100)
		double slopeAt100{ 0.0 };  // f'(100), sign must point towards L

		[[nodiscard]] double Tau() const noexcept
		{
			const double gap = valueAt100 - limit;
			const double s = std::abs(slopeAt100);
			return s > 0.0 ? std::abs(gap) / s : 0.0;
		}

		[[nodiscard]] double operator()(double a_skill) const noexcept
		{
			if (a_skill <= kVanillaCap) {
				return valueAt100;  // caller uses vanilla below 100
			}
			const double tau = Tau();
			if (tau <= 0.0) {
				return limit;
			}
			return limit + (valueAt100 - limit) * std::exp(-(a_skill - kVanillaCap) / tau);
		}
	};

	// ---------------------------------------------------------------------
	// Power-decay formulas: factor(s) = 1 - (s * base)^exponent
	// Used by spell cost (FR-25) and enchanting charge cost (FR-30).
	// ---------------------------------------------------------------------

	struct PowerDecayFormula
	{
		double base{ 1.0 / 400.0 };
		double exponent{ 0.65 };

		[[nodiscard]] double Vanilla(double a_skill) const noexcept
		{
			return 1.0 - std::pow(std::max(a_skill, 0.0) * base, exponent);
		}

		[[nodiscard]] double VanillaSlope(double a_skill) const noexcept
		{
			// d/ds [1 - (s b)^e] = -e b (s b)^(e-1)
			return -exponent * base * std::pow(a_skill * base, exponent - 1.0);
		}

		[[nodiscard]] Asymptote MakeAsymptote(double a_limit) const noexcept
		{
			return { a_limit, Vanilla(kVanillaCap), VanillaSlope(kVanillaCap) };
		}

		// Factor for any skill: vanilla up to 100, asymptotic above.
		[[nodiscard]] double Factor(double a_skill, double a_limit) const noexcept
		{
			return a_skill <= kVanillaCap ? Vanilla(a_skill) : MakeAsymptote(a_limit)(a_skill);
		}

		// Skill level that makes the vanilla formula return a_factor.
		// Inverse of Vanilla(): s = (1/base) * (1 - factor)^(1/exponent).
		[[nodiscard]] double InverseVanilla(double a_factor) const noexcept
		{
			const double x = std::clamp(1.0 - a_factor, 0.0, 1.0);
			return std::pow(x, 1.0 / exponent) / base;
		}

		// Effective skill to report to the engine so that it computes the
		// asymptotic factor instead of the vanilla one.
		[[nodiscard]] double EffectiveSkill(double a_skill, double a_limit) const noexcept
		{
			if (a_skill <= kVanillaCap) {
				return a_skill;
			}
			return InverseVanilla(MakeAsymptote(a_limit)(a_skill));
		}
	};

	// Spell cost skill factor. Vanilla GMST-equivalent constants: base 1/400, exponent 0.65.
	// The plugin reads the real values from the game at runtime.
	using SpellCostFormula = PowerDecayFormula;

	// Enchanting charge-per-use skill factor:
	// 1 - (s * fEnchantingSkillCostBase)^fEnchantingSkillCostScale.
	// Vanilla: base 0.005, scale 0.5 -> 0.293 at 100, 0 at 200, undefined above.
	[[nodiscard]] inline PowerDecayFormula EnchantChargeFormula(double a_base = 0.005, double a_scale = 0.5) noexcept
	{
		return { a_base, a_scale };
	}

	// ---------------------------------------------------------------------
	// Barter (FR-26)
	// ---------------------------------------------------------------------

	// Vanilla skill price factor: max - (max - min) * s / 100. Vanilla: max 3.3, min 2.0.
	struct BarterFormula
	{
		double factorMax{ 3.3 };
		double factorMin{ 2.0 };

		[[nodiscard]] double Vanilla(double a_skill) const noexcept
		{
			return factorMax - (factorMax - factorMin) * a_skill / 100.0;
		}

		[[nodiscard]] Asymptote MakeAsymptote(double a_limit) const noexcept
		{
			return { a_limit, Vanilla(kVanillaCap), -(factorMax - factorMin) / 100.0 };
		}
	};

	// Final clamp (FR-26): buy price never below a_minRatio x sell price.
	[[nodiscard]] inline double ClampBuyPrice(double a_buy, double a_sell, double a_minRatio) noexcept
	{
		return std::max(a_buy, a_sell * a_minRatio);
	}
}
