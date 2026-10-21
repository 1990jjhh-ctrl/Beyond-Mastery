#include "Config.h"

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

		xpGainFactorPerTier = ini.GetDoubleValue("General", "fXPGainFactorPerTier", xpGainFactorPerTier);
		clampCharXPAbove100 = ini.GetBoolValue("General", "bClampCharXPAbove100", clampCharXPAbove100);
		warnOnConflict = ini.GetBoolValue("General", "bWarnOnConflict", warnOnConflict);
		knownConflicts = SplitList(ini.GetValue("General", "sKnownConflicts", JoinList(knownConflicts).c_str()));

		enableFormulaScaling = ini.GetBoolValue("Scaling", "bEnableFormulaScaling", enableFormulaScaling);
		spellCostLimit = ini.GetDoubleValue("Scaling", "fSpellCostLimit", spellCostLimit);
		barterMinRatio = ini.GetDoubleValue("Scaling", "fBarterMinRatio", barterMinRatio);
		enchantChargeLimitFraction = ini.GetDoubleValue("Scaling", "fEnchantChargeLimitFraction", enchantChargeLimitFraction);

		// Validate (FR-9: f in (0, 1]).
		if (xpGainFactorPerTier <= 0.0 || xpGainFactorPerTier > 1.0) {
			logger::warn("fXPGainFactorPerTier = {} out of range (0, 1], clamping", xpGainFactorPerTier);
			xpGainFactorPerTier = std::clamp(xpGainFactorPerTier, 1e-6, 1.0);
		}
		spellCostLimit = std::clamp(spellCostLimit, 0.0, 0.59);
		barterMinRatio = std::max(barterMinRatio, 1.0);
		enchantChargeLimitFraction = std::clamp(enchantChargeLimitFraction, 0.01, 0.99);

		// Write back a complete, commented file.
		ini.SetDoubleValue("General", "fXPGainFactorPerTier", xpGainFactorPerTier,
			"; XP gained above 100 is multiplied by f^(T-1), T = floor(skill/100). Range (0, 1]. 1.0 = vanilla curve.");
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

		if (ini.SaveFile(kPath) < 0) {
			logger::warn("Could not write {}", kPath);
		}

		logger::info("Config: f={}, clampCharXP={}, warnOnConflict={}, scaling={}, spellLimit={}, barterRatio={}, enchantChargeFraction={}",
			xpGainFactorPerTier, clampCharXPAbove100, warnOnConflict, enableFormulaScaling, spellCostLimit, barterMinRatio, enchantChargeLimitFraction);
	}
}
