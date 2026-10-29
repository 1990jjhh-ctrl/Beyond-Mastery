#pragma once

// The Custom Skills Framework (CSF) skills Beyond Mastery can raise above 100:
// read from CSF's own JSON files, matched to their forms at kDataLoaded, and
// configured through a [CustomSkill.<id>] INI section each (FR-34). The INI
// parsing is in CustomSkillsData.h; the sections are read and written by
// Config.cpp.

#include "CustomSkillsData.h"

namespace BM::CustomSkills
{
	struct Skill
	{
		std::string                              id, name, file;  // file: name within CustomSkills/
		std::string                              displayName;     // name, translated as CSF shows it (set by Resolve)
		CustomSkillsData::FormRef                level;
		std::optional<CustomSkillsData::FormRef> legendary, perkPoints;  // perkPoints: the file's group pool
		RE::TESGlobal*                           levelGlobal{};
		RE::TESGlobal*                           legendaryGlobal{};
		RE::TESGlobal*                           perkPointsGlobal{};
		// Guarded by a lock: CSF's threads read it on every XP gain while
		// Papyrus threads may change it. Use SettingsOf and ApplySettings.
		CustomSkillsData::Settings settings;
	};

	// Parses Data/SKSE/Plugins/CustomSkills/*.json. A file that does not parse
	// is logged and skipped. Runs before the INI is read (Config::Load), so
	// the INI gets a section for each skill found.
	void Discover();

	// Looks up the globals of every skill; logs each form that is missing.
	// Needs the loaded data (kDataLoaded). With CSF absent it only logs that,
	// and no skill is enabled.
	void Resolve();

	[[nodiscard]] const std::vector<Skill>& All() noexcept;
	[[nodiscard]] Skill*                    FindById(std::string_view a_id) noexcept;
	[[nodiscard]] Skill*                    FindByLevelGlobal(const RE::TESGlobal* a_global) noexcept;
	// The skill whose displayName is a_name, i.e. the GetFullName of its
	// ActorValueInfo. nullptr if no skill has the name, or if more than one
	// does: a shared name cannot pick one of them.
	[[nodiscard]] Skill* FindByName(std::string_view a_name) noexcept;
	// Whether any skill is opted in and CSF is there to serve it.
	[[nodiscard]] bool AnyEnabled() noexcept;

	// Callee of the CSF level-limit stubs (Stubs::LevelLimitStub, FR-35): the
	// limit of the enabled skill whose level global is a_level, otherwise
	// vanilla CSF's 100. Called by CSF on every XP gain; never throws. Also
	// clears the skill's gates once its level is seen below 100 (FR-13).
	[[nodiscard]] std::int32_t LevelLimit(const RE::TESGlobal* a_level) noexcept;

	// Callee of the CSF use-XP stub (Stubs::UseXPFactorStub): the FR-9 factor
	// for an enabled skill's level with bScaleXPGainAbove100, otherwise 1.
	[[nodiscard]] float UseXPFactor(const RE::TESGlobal* a_level) noexcept;

	// A consistent copy of a skill's settings.
	[[nodiscard]] CustomSkillsData::Settings SettingsOf(const Skill& a_skill);
	// Replaces a skill's settings.
	void ApplySettings(Skill& a_skill, CustomSkillsData::Settings a_settings);

	// Changes one key (bEnable, sPerkSource) of a skill's section: validates
	// the whole section through ParseSettings, and only if it is clean
	// updates the skill and then saves the INI. False for an unknown skill or
	// key, or a bad value. If the save fails the change still stands in
	// memory (and is logged).
	[[nodiscard]] bool SetSetting(std::string_view a_id, std::string_view a_key, std::string_view a_value);
}
