#pragma once

// The Legendary menu (PRD FR-14 ... FR-17, FR-31).
//
// Pressing Legendary in the skill tree normally shows vanilla's yes/no box.
// Hooks.cpp redirects the call that creates that box to OpenLegendary, which
// shows Reset Skill / Upgrade / Cancel instead. Reset Skill hands back to the
// vanilla callback, so it stays exactly vanilla Legendary.

namespace BM::Menu
{
	// Call-site thunk with MessageBoxMenu::Create's exact signature: the
	// arguments are the box vanilla was about to show. Shows our menu when the
	// callback is the Legendary one, and vanilla's box unchanged otherwise.
	bool OpenLegendary(RE::BSString& a_message, const RE::BSTSmartPointer<RE::IMessageBoxCallback>& a_callback,
		std::uint8_t a_buttonPressOffset, std::int32_t a_warningType, std::int32_t a_menuDepth,
		const RE::BSTArray<RE::BSString>& a_buttons);
}
