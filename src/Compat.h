#pragma once

// Other SKSE plugins Beyond Mastery adapts to rather than warns about
// (PRD FR-32, FR-33).

namespace BM::Compat
{
	// Plugin names, as SKSE reports them.
	inline constexpr auto kExperience = "Experience";
	inline constexpr auto kCustomSkills = "CustomSkills";

	// Probes the loaded plugins. SKSE only answers from kPostLoad on, so call it
	// there, before the hooks are installed at kDataLoaded.
	void Detect(const SKSE::LoadInterface& a_skse);

	// Experience gates skill XP by player level and owns character XP, so the
	// character-XP clamp (FR-11) stays out while it is loaded.
	[[nodiscard]] bool ExperienceLoaded() noexcept;

	// Whether Custom Skills Framework is loaded; its skills are the ones the
	// opt-in INI sections can raise above 100.
	[[nodiscard]] bool CustomSkillsLoaded() noexcept;

	// Custom Skills Framework replaces the player's GetActorValue slot at its
	// own load and forwards every vanilla skill to the function it found there,
	// so formula scaling may wrap it (FR-33). True only if CSF is loaded and
	// a_address lies in its DLL.
	[[nodiscard]] bool IsInCustomSkills(std::uintptr_t a_address) noexcept;
}
