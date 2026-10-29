#pragma once

// INI settings (PRD FR-18, FR-28). File: Data/SKSE/Plugins/BeyondMastery.ini

#include "CustomSkillsData.h"

#include <functional>

namespace BM::CustomSkills
{
	struct Skill;
}

namespace BM
{
	struct Config
	{
		// [General]
		bool                     scaleXPGainAbove100{ false };  // FR-9
		bool                     clampCharXPAbove100{ false };  // FR-11
		bool                     warnOnConflict{ true };        // FR-20
		std::vector<std::string> knownConflicts{ "SkyrimUncapper" };

		// [Scaling] (FR-28)
		bool   enableFormulaScaling{ true };
		double spellCostLimit{ 0.25 };             // FR-25
		double barterMinRatio{ 1.2 };              // FR-26
		double enchantChargeLimitFraction{ 0.5 };  // FR-30, limit = fraction x value at 100

		static Config& Get() noexcept;

		// Loads the INI; missing file or keys keep defaults. Writes back a
		// complete file so new keys appear for the user: a new setting goes in
		// both the read and the write-back block of Config.cpp, with its
		// comment string.
		void Load();

		// One key of a [CustomSkill.<id>] section (see CustomSkills::SetSetting).
		// Only if the whole section stays valid: hands the new settings to
		// a_apply (which stores them, under the lock that guards them), then
		// saves the INI.
		[[nodiscard]] bool SetCustomSkillValue(const CustomSkills::Skill& a_skill, std::string_view a_key, std::string_view a_value,
			const std::function<void(CustomSkillsData::Settings)>& a_apply);
	};
}
