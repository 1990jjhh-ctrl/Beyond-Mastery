#include "Compat.h"

namespace BM::Compat
{
	namespace
	{
		bool g_experienceLoaded = false;
	}

	void Detect(const SKSE::LoadInterface& a_skse)
	{
		// SKSE's own accessor, not SKSE::GetPluginInfo: see DetectConflicts in
		// main.cpp for why.
		g_experienceLoaded = a_skse.GetPluginInfo(kExperience) != nullptr;
		if (g_experienceLoaded) {
			logger::info("{} is loaded: it caps skills by player level, Beyond Mastery above that (FR-32)", kExperience);
		}
	}

	bool ExperienceLoaded() noexcept
	{
		return g_experienceLoaded;
	}
}
