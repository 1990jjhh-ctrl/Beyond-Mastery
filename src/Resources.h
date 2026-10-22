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

	// What the player holds right now.
	[[nodiscard]] Holdings Read();

	// Filled black soul gems available to pay with: The Black Star counts as one.
	[[nodiscard]] std::uint32_t GemsAvailable(const Holdings& a_holdings) noexcept;

	// Spends a_cost, paying its gems the way a_gems says. Re-reads the player
	// first and spends nothing unless everything is still covered (FR-5).
	bool Spend(const Curves::StepCost& a_cost, const Curves::GemPlan& a_gems);
}
