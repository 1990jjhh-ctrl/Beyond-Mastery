#include "Compat.h"

#include "CustomSkillsSites.h"

namespace BM::Compat
{
	namespace
	{
		bool g_experienceLoaded = false;
		bool g_customSkillsLoaded = false;
	}

	void Detect(const SKSE::LoadInterface& a_skse)
	{
		// SKSE's own accessor, not SKSE::GetPluginInfo: see DetectConflicts in
		// main.cpp for why.
		g_experienceLoaded = a_skse.GetPluginInfo(kExperience) != nullptr;
		if (g_experienceLoaded) {
			logger::info("{} is loaded: it caps skills by player level, Beyond Mastery above that (FR-32)", kExperience);
		}
		g_customSkillsLoaded = a_skse.GetPluginInfo(kCustomSkills) != nullptr;
		if (g_customSkillsLoaded) {
			logger::info("{} is loaded: its skills stay its own, Beyond Mastery covers the vanilla ones (FR-33)", kCustomSkills);
		}
	}

	bool ExperienceLoaded() noexcept
	{
		return g_experienceLoaded;
	}

	bool CustomSkillsLoaded() noexcept
	{
		return g_customSkillsLoaded;
	}

	bool IsInCustomSkills(std::uintptr_t a_address) noexcept
	{
		if (!g_customSkillsLoaded) {
			return false;
		}
		const auto                         module = REX::W32::GetModuleHandleW(CustomSkillsSites::kModule);
		REX::W32::MEMORY_BASIC_INFORMATION info{};
		return module && REX::W32::VirtualQuery(reinterpret_cast<const void*>(a_address), &info, sizeof(info)) != 0 &&
		       info.allocationBase == module;
	}
}
