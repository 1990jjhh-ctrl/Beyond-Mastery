#pragma once

// Pure math for Beyond Mastery. No game or CommonLib dependencies, so this
// header is unit-tested natively on Linux (tests/test_curves.cpp).
//
// PRD references: §6.2 (costs), FR-9 (XP gain), §6.9 (formula scaling).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

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

	// Perk points charged per step, whatever the step number (PRD v1.2 §6.2).
	// An escalating cost made grind the binding constraint and overflowed the
	// engine's uint8 perkCount past step 255; level progression is the gate now.
	inline constexpr std::uint32_t kPerksPerStep = 1;

	// Cost of raising the cap from a_currentCap (a multiple of 10, >= 100) by +10.
	[[nodiscard]] inline StepCost CostOfNextStep(std::uint32_t a_currentCap) noexcept
	{
		const auto t = Tier(static_cast<double>(a_currentCap));
		StepCost   cost;
		cost.perks = kPerksPerStep;
		cost.blackSoulGems = t >= 1u ? t - 1u : 0u;
		cost.dragonSouls = (a_currentCap % 100u == 0u) ? 10u * t : 0u;
		return cost;
	}

	// ---------------------------------------------------------------------
	// Cap enforcement (FR-2, FR-8)
	// ---------------------------------------------------------------------

	// Highest cap accepted from a co-save. FR-4 puts no limit on the cap
	// itself -- the soft cap is cost and the XP curve -- but a value read back
	// from a save is data we did not write, and it goes straight into the
	// engine's level comparison, so a corrupt one would let a skill rise
	// without bound.
	inline constexpr std::uint16_t kMaxPlausibleCap = 10000;

	// A cap is a multiple of 10, at least the vanilla 100, and within range.
	// Anything else is damaged data rather than a cap (FR-22).
	[[nodiscard]] inline bool IsPlausibleCap(std::uint16_t a_cap) noexcept
	{
		return a_cap >= 100 && a_cap <= kMaxPlausibleCap && a_cap % 10 == 0;
	}

	// The level a skill may actually reach: never above its cap.
	//
	// A level already past the cap is a real state, not an error: uninstalling
	// leaves skills above 100 (the engine stores them as floats) and a lost
	// co-save puts every cap back to 100 (FR-22, FR-23). Clamp down instead of
	// treating it as a fault. A negative level is the engine's business, so it
	// passes through untouched rather than being invented away.
	[[nodiscard]] inline double ClampLevelToCap(double a_level, std::uint16_t a_cap) noexcept
	{
		return std::min(a_level, static_cast<double>(a_cap));
	}

	// FR-2: a cap may only be raised once the level has reached it. A level
	// above the cap still counts, so the state above cannot strand a skill.
	[[nodiscard]] inline bool AtCap(double a_level, std::uint16_t a_cap) noexcept
	{
		return a_level >= static_cast<double>(a_cap);
	}

	// ---------------------------------------------------------------------
	// Limit, gates and upgrades (PRD v1.4: §5, §6.2, FR-2, FR-13)
	// ---------------------------------------------------------------------

	// The level a skill can reach right now: its cap, or the first gate not yet
	// paid this climb, whichever is lower. Equal to the cap until Reset Skill
	// clears the paid gates.
	[[nodiscard]] inline std::uint16_t Limit(std::uint16_t a_cap, std::uint8_t a_gatesPaid) noexcept
	{
		const std::uint32_t gateLimit = 100u * (1u + a_gatesPaid);
		return static_cast<std::uint16_t>(std::min<std::uint32_t>(a_cap, gateLimit));
	}

	// Gates a cap has passed, ceil((cap - 100) / 100). Also the default for a
	// save written before gates recurred: with this many paid, Limit == cap, so
	// a cap bought earlier is never suddenly locked at the next 100.
	[[nodiscard]] inline std::uint8_t GatesNeededFor(std::uint16_t a_cap) noexcept
	{
		return a_cap <= 100 ? std::uint8_t{ 0 } : static_cast<std::uint8_t>((a_cap - 100 + 99) / 100);
	}

	// Dragon souls paid for the gates of the current climb: gate T costs 10 x T,
	// so gates 1..n cost 10 x n(n+1)/2. What Reset Skill forfeits (FR-13).
	[[nodiscard]] inline std::uint32_t DragonSoulsPaid(std::uint8_t a_gatesPaid) noexcept
	{
		return 10u * a_gatesPaid * (a_gatesPaid + 1u) / 2u;
	}

	enum class UpgradeKind : std::uint8_t
	{
		kGate,        // pay an unpaid gate below the cap; the cap is unchanged
		kStep,        // raise the cap by 10
		kStepAndGate  // raise the cap across a 100-level boundary not yet paid
	};

	struct Upgrade
	{
		UpgradeKind   kind{ UpgradeKind::kStep };
		StepCost      cost{};
		std::uint16_t newCap{ 0 };
		std::uint8_t  newGatesPaid{ 0 };
	};

	// What Upgrade does for a skill standing at its limit (FR-2).
	[[nodiscard]] inline Upgrade UpgradeFor(std::uint16_t a_cap, std::uint8_t a_gatesPaid) noexcept
	{
		const auto limit = Limit(a_cap, a_gatesPaid);
		if (limit < a_cap) {
			const std::uint32_t gate = limit / 100u;
			return { UpgradeKind::kGate, { 0, 0, 10u * gate }, a_cap,
				static_cast<std::uint8_t>(a_gatesPaid + 1) };
		}

		// The cap is binding: an ordinary step, plus the gate's dragon souls when
		// the step leaves a 100-level boundary still unpaid this climb.
		const auto          step = CostOfNextStep(a_cap);
		Upgrade             upgrade{ UpgradeKind::kStep, { step.perks, step.blackSoulGems, 0 },
			static_cast<std::uint16_t>(a_cap + 10), a_gatesPaid };
		const std::uint32_t gate = a_cap / 100u;
		if (a_cap % 100u == 0 && a_gatesPaid < gate) {
			upgrade.kind = UpgradeKind::kStepAndGate;
			upgrade.cost.dragonSouls = 10u * gate;
			upgrade.newGatesPaid = static_cast<std::uint8_t>(gate);
		}
		return upgrade;
	}

	// Which filled black soul gems pay for a_needed (FR-6, FR-7). The Black Star
	// counts as one gem and is used only when the player chooses to; it is
	// emptied rather than consumed, so it is never spent without asking.
	struct GemPlan
	{
		std::uint32_t consumable{ 0 };
		bool          blackStar{ false };
	};

	[[nodiscard]] inline std::optional<GemPlan> PlanGems(std::uint32_t a_needed, std::uint32_t a_consumable,
		bool a_blackStarFilled, bool a_useBlackStar) noexcept
	{
		const bool          star = a_useBlackStar && a_blackStarFilled && a_needed > 0;
		const std::uint32_t fromConsumable = a_needed - (star ? 1u : 0u);
		if (fromConsumable > a_consumable) {
			return std::nullopt;
		}
		return GemPlan{ fromConsumable, star };
	}

	// FR-4 puts no limit on the cap, but the co-save loader resets any cap above
	// kMaxPlausibleCap to 100, so a raise past it would be lost on the next
	// reload. Upgrade stops there instead.
	[[nodiscard]] inline bool CapRaisable(std::uint16_t a_cap) noexcept
	{
		return a_cap + 10u <= kMaxPlausibleCap;
	}

	// ---------------------------------------------------------------------
	// Skill XP gain above 100 (FR-9)
	// ---------------------------------------------------------------------

	// Multiplier on the skill XP gained from use at base level a_level:
	// sqrt(100 / level) above 100 -- the slope of form A, so a level earns XP
	// at the rate it adds effect -- and 1 up to 100. Continuous at 100, never
	// above 1, never increasing.
	[[nodiscard]] inline double XPGainMultiplier(double a_level) noexcept
	{
		if (!(a_level > kVanillaCap)) {
			return 1.0;
		}
		return std::sqrt(kVanillaCap / a_level);
	}

	// XP needed to reach the next level of a skill at a_level, the vanilla
	// formula extended past 100 (FR-9). ImproveMult and ImproveOffset are
	// per-skill game settings and fSkillUseCurve is global; all three are read
	// from the game at runtime rather than hardcoded.
	[[nodiscard]] inline double XPThreshold(double a_level, double a_improveMult,
		double a_skillUseCurve, double a_improveOffset) noexcept
	{
		return a_improveMult * std::pow(a_level, a_skillUseCurve) + a_improveOffset;
	}

	// Largest integer a float can still represent exactly: 2^24. Past it,
	// consecutive integers collapse onto the same float, so XP accumulated in
	// single precision would silently stop advancing (FR-10).
	inline constexpr double kFloatIntegerLimit = 16777216.0;

	[[nodiscard]] inline bool ThresholdNeedsDouble(double a_threshold) noexcept
	{
		return a_threshold > kFloatIntegerLimit;
	}

	// FR-5: a step is only affordable when every resource is covered. Checked
	// in full before anything is deducted, so a missing resource costs nothing.
	[[nodiscard]] inline bool Affordable(const StepCost& a_cost, std::uint32_t a_perks,
		std::uint32_t a_blackSoulGems, std::uint32_t a_dragonSouls) noexcept
	{
		return a_perks >= a_cost.perks &&
		       a_blackSoulGems >= a_cost.blackSoulGems &&
		       a_dragonSouls >= a_cost.dragonSouls;
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

	// Inverse of SqrtEffectiveSkill: the real skill a form A value came from.
	[[nodiscard]] inline double RealSkillFromSqrt(double a_effective) noexcept
	{
		if (a_effective <= kVanillaCap) {
			return a_effective;
		}
		const double root = (a_effective - kVanillaCap) / (2.0 * kVanillaCap) + 1.0;
		return kVanillaCap * root * root;
	}

	// ---------------------------------------------------------------------
	// Form B: asymptotic continuation (PRD §6.9 B)
	// ---------------------------------------------------------------------

	// f(s) = L + (f100 - L) * exp(-(s - 100) / tau), tau = (f100 - L) / |f'100|.
	// Matches value and slope of the vanilla curve at s = 100.
	struct Asymptote
	{
		double limit{ 0.0 };       // L
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
		// asymptotic factor instead of the vanilla one. A limit at or above
		// the value at 100 (config, or a mod changing the GMSTs) leaves no
		// room to improve: the skill then counts as 100, never less.
		[[nodiscard]] double EffectiveSkill(double a_skill, double a_limit) const noexcept
		{
			if (a_skill <= kVanillaCap) {
				return a_skill;
			}
			if (a_limit >= Vanilla(kVanillaCap)) {
				return kVanillaCap;
			}
			return std::max(InverseVanilla(MakeAsymptote(a_limit)(a_skill)), kVanillaCap);
		}
	};

	// The skill to hand the engine's spell-cost function (FR-25). Every skill
	// reaches it as int(form A) -- the remap is form A for all of them, and a
	// modded effect can tie spell cost to any skill -- so the real skill is
	// recovered and form B applied to it, the same curve as the magic schools.
	// The engine truncates to int, which is also the resolution near the limit.
	[[nodiscard]] inline std::int32_t SpellCostEngineSkill(std::int32_t a_incoming, const PowerDecayFormula& a_formula,
		double a_limit) noexcept
	{
		if (a_incoming <= static_cast<std::int32_t>(kVanillaCap)) {
			return a_incoming;
		}
		const double real = RealSkillFromSqrt(a_incoming);
		return static_cast<std::int32_t>(std::floor(a_formula.EffectiveSkill(real, a_limit)));
	}

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

		// Skill level that makes the vanilla formula return a_factor.
		[[nodiscard]] double InverseVanilla(double a_factor) const noexcept
		{
			return 100.0 * (factorMax - a_factor) / (factorMax - factorMin);
		}

		// Effective Speech to report to the engine so that it computes the
		// asymptotic buy factor instead of the vanilla one. As for spell cost,
		// a limit at or above the value at 100 keeps Speech at 100.
		[[nodiscard]] double EffectiveSkill(double a_skill, double a_limit) const noexcept
		{
			if (a_skill <= kVanillaCap) {
				return a_skill;
			}
			if (a_limit >= Vanilla(kVanillaCap)) {
				return kVanillaCap;
			}
			return std::max(InverseVanilla(MakeAsymptote(a_limit)(a_skill)), kVanillaCap);
		}
	};

	// Final clamp (FR-26): buy price never below a_minRatio x sell price.
	[[nodiscard]] inline double ClampBuyPrice(double a_buy, double a_sell, double a_minRatio) noexcept
	{
		return std::max(a_buy, a_sell * a_minRatio);
	}

	struct BarterPrices
	{
		double buy{ 1.0 };
		double sell{ 1.0 };
	};

	// The engine's final barter clamp, after perks (fMinBuyMult, fMaxSellMult
	// and 1.0), followed by the ratio floor FR-26 adds.
	[[nodiscard]] inline BarterPrices BarterFinalClamp(double a_buy, double a_sell, double a_minBuyMult,
		double a_maxSellMult, double a_minRatio) noexcept
	{
		const double buy = std::max({ a_minBuyMult, a_buy, 1.0 });
		const double sell = std::min({ a_maxSellMult, a_sell, 1.0 });
		return { ClampBuyPrice(buy, sell, a_minRatio), sell };
	}

	// ---------------------------------------------------------------------
	// GetClampedActorValue (hook-points.md, "Formula scaling")
	// ---------------------------------------------------------------------

	// ActorValueInfo flags the engine's clamp reads.
	inline constexpr std::uint32_t kSpecialStatClampsAsNonZero = 1u << 2;
	inline constexpr std::uint32_t kClampAsSpecialStat = 1u << 3;
	inline constexpr std::uint32_t kClampAsSkill = 1u << 4;

	// SSE minss / maxss, which the engine's clamp uses: the second operand
	// wins unless the first is strictly smaller (larger), so NaN gives way to
	// the bound. std::min/std::max would keep the NaN instead.
	[[nodiscard]] constexpr double Minss(double a_value, double a_bound) noexcept
	{
		return a_value < a_bound ? a_value : a_bound;
	}

	[[nodiscard]] constexpr double Maxss(double a_value, double a_bound) noexcept
	{
		return a_value > a_bound ? a_value : a_bound;
	}

	// The engine's clamp of an actor value by its flags: skills to [0, 100],
	// special stats to [0 or 1, 10], anything else unchanged -- with the
	// engine's NaN and infinity behaviour. With a_liftSkillCeiling -- the
	// player's skills -- a finite skill is only kept >= 0, so values above
	// 100 reach the formulas (FR-24); a non-finite one is clamped as vanilla.
	[[nodiscard]] inline double ClampActorValue(double a_value, std::uint32_t a_flags, bool a_liftSkillCeiling) noexcept
	{
		if (a_flags & kClampAsSkill) {
			if (a_liftSkillCeiling && std::isfinite(a_value)) {
				return Maxss(a_value, 0.0);
			}
			return Maxss(Minss(a_value, kVanillaCap), 0.0);
		}
		if (a_flags & kClampAsSpecialStat) {
			const double floor = (a_flags & kSpecialStatClampsAsNonZero) ? 1.0 : 0.0;
			return Maxss(Minss(a_value, 10.0), floor);
		}
		return a_value;
	}
}
