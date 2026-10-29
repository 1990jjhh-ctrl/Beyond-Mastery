#include "Config.h"

#include "CustomSkills.h"

#include <SimpleIni.h>

#include <algorithm>
#include <sstream>

namespace BM
{
	namespace
	{
		constexpr auto kPath = "Data/SKSE/Plugins/BeyondMastery.ini";

		std::vector<std::string> SplitList(std::string_view a_list)
		{
			std::vector<std::string> out;
			std::string              item;
			std::istringstream       ss{ std::string(a_list) };
			while (std::getline(ss, item, ',')) {
				const auto b = item.find_first_not_of(" \t");
				const auto e = item.find_last_not_of(" \t");
				if (b != std::string::npos) {
					out.emplace_back(item.substr(b, e - b + 1));
				}
			}
			return out;
		}

		std::string JoinList(const std::vector<std::string>& a_list)
		{
			std::string out;
			for (const auto& s : a_list) {
				if (!out.empty()) {
					out += ", ";
				}
				out += s;
			}
			return out;
		}

		using namespace CustomSkillsData;

		RawSettings ReadRaw(const CSimpleIniA& a_ini, const std::string& a_section)
		{
			const auto get = [&](const char* a_key) { return std::string(a_ini.GetValue(a_section.c_str(), a_key, "")); };
			return { get("bEnable"), get("sPerkSource") };
		}

		// A key that fell back to its default keeps the player's own text, so
		// a value that is only unusable right now (e.g. "own" while the CSF
		// file has no perk pool) is not lost. The setting itself stays at the
		// fallback.
		std::string Keep(bool a_isDefault, const std::string& a_raw, std::string a_canonical)
		{
			return a_isDefault && !a_raw.empty() ? a_raw : a_canonical;
		}

		// Complete section: every key, with its comment.
		void WriteSection(CSimpleIniA& a_ini, const CustomSkills::Skill& a_skill, const RawSettings& a_raw)
		{
			const auto  section = SectionName(a_skill.id);
			const auto& s = a_skill.settings;
			const auto  set = [&](const char* a_key, const std::string& a_value, const char* a_comment) {
				a_ini.SetValue(section.c_str(), a_key, a_value.c_str(), a_comment);
			};
			set("bEnable", Keep(!s.enable, a_raw.enable, s.enable ? "true" : "false"),
				std::format("; {} (from CustomSkills/{})\n; Raise this skill's cap above 100 like the vanilla skills.", a_skill.name, a_skill.file).c_str());
			set("sPerkSource", Keep(s.perkSource == PerkSource::kPlayer, a_raw.perkSource, s.perkSource == PerkSource::kOwn ? "own" : "player"),
				"; Perk points that pay for the steps: player or own (needs a perk pool of its own).");
		}

		// a_lenient: a bad value falls back to its default and is logged (the
		// file as the player left it). Otherwise any warning rejects the lot.
		bool Parse(CustomSkills::Skill& a_skill, const RawSettings& a_raw, bool a_lenient)
		{
			auto parsed = ParseSettings(a_raw, a_skill.perkPoints.has_value());
			if (!a_lenient && !parsed.warnings.empty()) {
				return false;
			}
			for (const auto& w : parsed.warnings) {
				logger::warn("Custom skill {}: {}", a_skill.id, w);
			}
			a_skill.settings = std::move(parsed.settings);
			return true;
		}
	}

	Config& Config::Get() noexcept
	{
		static Config instance;
		return instance;
	}

	void Config::Load()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		const bool exists = ini.LoadFile(kPath) >= 0;
		if (!exists) {
			logger::info("{} not found, using defaults", kPath);
		}

		scaleXPGainAbove100 = ini.GetBoolValue("General", "bScaleXPGainAbove100", scaleXPGainAbove100);
		// Replaced by bScaleXPGainAbove100 (PRD v1.10); dropped from the file.
		ini.Delete("General", "fXPGainFactorPerTier", true);
		clampCharXPAbove100 = ini.GetBoolValue("General", "bClampCharXPAbove100", clampCharXPAbove100);
		warnOnConflict = ini.GetBoolValue("General", "bWarnOnConflict", warnOnConflict);
		knownConflicts = SplitList(ini.GetValue("General", "sKnownConflicts", JoinList(knownConflicts).c_str()));
		// Earlier defaults listed Experience and Custom Skills Framework, which
		// are now supported (FR-32, FR-33). Only an untouched old default is
		// updated; an edited list is the player's own.
		using List = std::vector<std::string>;
		if (knownConflicts == List{ "SkyrimUncapper", "Experience", "CustomSkills" } ||
			knownConflicts == List{ "SkyrimUncapper", "CustomSkills" }) {
			knownConflicts = Config{}.knownConflicts;
		}

		enableFormulaScaling = ini.GetBoolValue("Scaling", "bEnableFormulaScaling", enableFormulaScaling);
		spellCostLimit = ini.GetDoubleValue("Scaling", "fSpellCostLimit", spellCostLimit);
		barterMinRatio = ini.GetDoubleValue("Scaling", "fBarterMinRatio", barterMinRatio);
		enchantChargeLimitFraction = ini.GetDoubleValue("Scaling", "fEnchantChargeLimitFraction", enchantChargeLimitFraction);

		// Validate.
		spellCostLimit = std::clamp(spellCostLimit, 0.0, 0.59);
		barterMinRatio = std::max(barterMinRatio, 1.0);
		enchantChargeLimitFraction = std::clamp(enchantChargeLimitFraction, 0.01, 0.99);

		// Custom skills: sections of skills that are not installed stay as they are.
		CustomSkills::Discover();
		for (const auto& found : CustomSkills::All()) {
			const auto skill = CustomSkills::FindById(found.id);
			Parse(*skill, ReadRaw(ini, SectionName(found.id)), true);
		}

		// Write back a complete, commented file.
		ini.SetBoolValue("General", "bScaleXPGainAbove100", scaleXPGainAbove100,
			"; Above 100, skill XP gained from use is multiplied by sqrt(100/skill) (0.71 at 200, 0.5 at 400). Trainers and skill books stay exact.");
		ini.SetBoolValue("General", "bClampCharXPAbove100", clampCharXPAbove100,
			"; Clamp character XP from skill-ups above 100 to the level-100 value.");
		ini.SetBoolValue("General", "bWarnOnConflict", warnOnConflict,
			"; Show an in-game warning if a known conflicting SKSE plugin is loaded.");
		ini.SetValue("General", "sKnownConflicts", JoinList(knownConflicts).c_str(),
			"; Comma-separated SKSE plugin names treated as conflicts.");
		ini.SetBoolValue("Scaling", "bEnableFormulaScaling", enableFormulaScaling,
			"; Sub-linear / asymptotic continuation of skill formulas above 100.");
		ini.SetDoubleValue("Scaling", "fSpellCostLimit", spellCostLimit,
			"; Limit of the spell cost skill factor as skill -> infinity (vanilla at 100: 0.594).");
		ini.SetDoubleValue("Scaling", "fBarterMinRatio", barterMinRatio,
			"; Minimum ratio buy price / sell price for the same item.");
		ini.SetDoubleValue("Scaling", "fEnchantChargeLimitFraction", enchantChargeLimitFraction,
			"; Enchanting charge cost factor approaches this fraction of its value at skill 100 (vanilla at 100: 0.293).");

		for (const auto& skill : CustomSkills::All()) {
			const auto raw = ReadRaw(ini, SectionName(skill.id));
			WriteSection(ini, skill, raw);
		}

		if (ini.SaveFile(kPath) < 0) {
			logger::warn("Could not write {}", kPath);
		}

		logger::info("Config: scaleXPGain={}, clampCharXP={}, warnOnConflict={}, scaling={}, spellLimit={}, barterRatio={}, enchantChargeFraction={}",
			scaleXPGainAbove100, clampCharXPAbove100, warnOnConflict, enableFormulaScaling, spellCostLimit, barterMinRatio, enchantChargeLimitFraction);
	}

	bool Config::SetCustomSkillValue(CustomSkills::Skill& a_skill, std::string_view a_key, std::string_view a_value)
	{
		static constexpr std::array keys{ "bEnable"sv, "sPerkSource"sv };
		if (std::ranges::find(keys, a_key) == keys.end()) {
			return false;
		}
		CSimpleIniA ini;
		ini.SetUnicode();
		if (ini.LoadFile(kPath) < 0) {
			logger::warn("Could not read {}", kPath);
			return false;
		}
		const auto section = SectionName(a_skill.id);
		ini.SetValue(section.c_str(), std::string(a_key).c_str(), std::string(a_value).c_str());
		const auto raw = ReadRaw(ini, section);
		auto       candidate = a_skill;
		if (!Parse(candidate, raw, false)) {
			logger::warn("Custom skill {}: {} \"{}\" rejected", a_skill.id, a_key, a_value);
			return false;
		}
		a_skill.settings = std::move(candidate.settings);
		WriteSection(ini, a_skill, raw);
		if (ini.SaveFile(kPath) < 0) {
			logger::warn("Could not write {}", kPath);
		}
		return true;
	}
}
