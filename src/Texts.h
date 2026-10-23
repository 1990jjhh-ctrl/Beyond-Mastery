#pragma once

// Player-facing text that depends on state. Game-free and header-only, like
// Curves.h, so it is unit-tested natively (tests/test_texts.cpp).

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace BM::Texts
{
	// FR-31: the text of vanilla's second Legendary box, which Reset Skill
	// shows. a_resetLevel is fLegendarySkillResetValue; a_souls is what the
	// current climb's gates cost, lost on reset, and left out when zero.
	[[nodiscard]] inline std::string ResetConfirm(std::string_view a_skill, int a_resetLevel, std::uint16_t a_cap,
		std::uint32_t a_souls)
	{
		auto text = std::format("{} goes back to {} and its perk points are returned. Its cap stays at {}", a_skill,
			a_resetLevel, a_cap);
		if (a_souls > 0) {
			text += std::format(", but the {} dragon souls spent on it are lost", a_souls);
		}
		return text + ".\n\nAre you sure you want to reset this skill?";
	}
}
