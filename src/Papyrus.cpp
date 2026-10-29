#include "Papyrus.h"

#include "Compat.h"
#include "CustomSkills.h"
#include "Hooks.h"

// The natives of papyrus/BeyondMastery.psc. They change settings only through
// CustomSkills::SetSetting, which validates and saves, so a rejected value
// leaves both the state and the INI as they were.

namespace
{
	using namespace BM;

	constexpr auto kClass = "BeyondMastery"sv;

	// Empty without CSF: its skills are never resolved, so their settings
	// would do nothing.
	std::vector<std::string> GetCustomSkillIds(RE::StaticFunctionTag*)
	{
		std::vector<std::string> ids;
		if (!Compat::CustomSkillsLoaded()) {
			return ids;
		}
		for (const auto& skill : CustomSkills::All()) {
			ids.push_back(skill.id);
		}
		return ids;
	}

	bool CustomSkillLimitInstalled(RE::StaticFunctionTag*)
	{
		return Hooks::CustomSkillLimitInstalled();
	}

	std::string GetCustomSkillName(RE::StaticFunctionTag*, std::string_view a_id)
	{
		const auto* skill = CustomSkills::FindById(a_id);
		return skill ? skill->displayName : std::string(a_id);
	}

	bool GetCustomSkillEnabled(RE::StaticFunctionTag*, std::string_view a_id)
	{
		const auto* skill = CustomSkills::FindById(a_id);
		return skill && CustomSkills::SettingsOf(*skill).enable;
	}

	// Refuses any key the MCM may not change, however it got here.
	bool Set(std::string_view a_id, std::string_view a_key, std::string_view a_value)
	{
		return CustomSkillsData::IsMcmKey(a_key) && CustomSkills::SetSetting(a_id, a_key, a_value);
	}

	bool SetCustomSkillEnabled(RE::StaticFunctionTag*, std::string_view a_id, bool a_enabled)
	{
		return Set(a_id, "bEnable", a_enabled ? "true" : "false");
	}

	bool CustomSkillHasOwnPool(RE::StaticFunctionTag*, std::string_view a_id)
	{
		const auto* skill = CustomSkills::FindById(a_id);
		return skill && skill->perkPointsGlobal;
	}

	std::string GetCustomSkillPerkSource(RE::StaticFunctionTag*, std::string_view a_id)
	{
		const auto* skill = CustomSkills::FindById(a_id);
		const bool  own = skill && CustomSkills::SettingsOf(*skill).perkSource == CustomSkillsData::PerkSource::kOwn;
		return own ? "own" : "player";
	}

	bool SetCustomSkillPerkSource(RE::StaticFunctionTag*, std::string_view a_id, std::string_view a_source)
	{
		return Set(a_id, "sPerkSource", a_source);
	}
}

bool BM::Papyrus::Register(RE::BSScript::IVirtualMachine* a_vm)
{
	a_vm->RegisterFunction("GetCustomSkillIds"sv, kClass, GetCustomSkillIds);
	a_vm->RegisterFunction("CustomSkillLimitInstalled"sv, kClass, CustomSkillLimitInstalled);
	a_vm->RegisterFunction("GetCustomSkillName"sv, kClass, GetCustomSkillName);
	a_vm->RegisterFunction("GetCustomSkillEnabled"sv, kClass, GetCustomSkillEnabled);
	a_vm->RegisterFunction("SetCustomSkillEnabled"sv, kClass, SetCustomSkillEnabled);
	a_vm->RegisterFunction("CustomSkillHasOwnPool"sv, kClass, CustomSkillHasOwnPool);
	a_vm->RegisterFunction("GetCustomSkillPerkSource"sv, kClass, GetCustomSkillPerkSource);
	a_vm->RegisterFunction("SetCustomSkillPerkSource"sv, kClass, SetCustomSkillPerkSource);
	return true;
}
