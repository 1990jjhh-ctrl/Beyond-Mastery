#pragma once

// Other SKSE plugins Beyond Mastery adapts to rather than warns about (PRD FR-32).

namespace BM::Compat
{
	// Experience's plugin name, as SKSE reports it.
	inline constexpr auto kExperience = "Experience";

	// Probes the loaded plugins. SKSE only answers from kPostLoad on, so call it
	// there, before the hooks are installed at kDataLoaded.
	void Detect(const SKSE::LoadInterface& a_skse);

	// Experience gates skill XP by player level and owns character XP, so the
	// character-XP clamp (FR-11) stays out while it is loaded.
	[[nodiscard]] bool ExperienceLoaded() noexcept;
}
