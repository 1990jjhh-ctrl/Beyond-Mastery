#pragma once

#include "Curves.h"

// The player's side of an upgrade: what they hold, and spending it
// (PRD FR-5 ... FR-7).

namespace BM::Resources
{
	struct Holdings
	{
		std::uint32_t perks{ 0 };
		std::uint32_t dragonSouls{ 0 };
		std::uint32_t consumableGems{ 0 };  // filled black soul gems, The Black Star apart
		bool          blackStarFilled{ false };
	};

	// What the player holds right now. a_perkPool: the global that holds the
	// perk points to pay with (a custom skill's own pool), read as a
	// non-negative whole number; nullptr for the player's perk points.
	[[nodiscard]] Holdings Read(RE::TESGlobal* a_perkPool);

	// Filled black soul gems available to pay with: The Black Star counts as one.
	[[nodiscard]] std::uint32_t GemsAvailable(const Holdings& a_holdings) noexcept;

	enum class SpendResult : std::uint8_t
	{
		kSpent,             // everything taken; apply the upgrade
		kNotAffordable,     // the re-check failed; nothing was taken (FR-5)
		kGemRemovalFailed,  // the engine would not take a gem; perks and souls untouched,
							// but gems removed before the failure stay removed
	};

	// Spends a_cost, paying its gems the way a_gems says. Re-reads the player
	// first and takes nothing unless everything is still covered (FR-5). The
	// perks come out of a_perkPool, as for Read.
	[[nodiscard]] SpendResult Spend(const Curves::StepCost& a_cost, const Curves::GemPlan& a_gems,
		RE::TESGlobal* a_perkPool);
}
