#include "Scaling.h"

#include "Config.h"
#include "Curves.h"
#include "Skills.h"

#include <atomic>

namespace BM::Scaling
{
	namespace
	{
		// Formula constants, read from the game at kDataLoaded. The defaults
		// are Skyrim.esm's values, kept only for a setting that is missing.
		struct Settings
		{
			double spellCostBase{ 0.0025 };  // fMagicCasterPCSkillCostBase
			double spellCostScale{ 0.65 };   // fMagicPCSkillCostScale
			double barterMax{ 3.3 };         // fBarterMax
			double barterMin{ 2.0 };         // fBarterMin
		};

		Settings g_settings;

		using GetActorValue_t = float (*)(RE::ActorValueOwner*, RE::ActorValue);
		GetActorValue_t g_original = nullptr;

		// Open skills-menu scopes. GetActorValue runs on several threads, so
		// this is atomic; while the menu builds its list, other threads
		// reading the player's skill briefly see the real value too.
		std::atomic<int> g_menuScopes{ 0 };

		[[nodiscard]] double ReadSetting(const char* a_name, double a_fallback)
		{
			auto*       settings = RE::GameSettingCollection::GetSingleton();
			const auto* setting = settings ? settings->GetSetting(a_name) : nullptr;
			if (!setting) {
				logger::warn("{} not found; using {}", a_name, a_fallback);
				return a_fallback;
			}
			return setting->GetFloat();
		}

		[[nodiscard]] RE::ActorValueOwner* PlayerAVO()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			return player ? player->AsActorValueOwner() : nullptr;
		}
	}

	void CacheSettings()
	{
		g_settings.spellCostBase = ReadSetting("fMagicCasterPCSkillCostBase", g_settings.spellCostBase);
		g_settings.spellCostScale = ReadSetting("fMagicPCSkillCostScale", g_settings.spellCostScale);
		g_settings.barterMax = ReadSetting("fBarterMax", g_settings.barterMax);
		g_settings.barterMin = ReadSetting("fBarterMin", g_settings.barterMin);
		logger::info("Scaling settings: spell cost base {} scale {}, barter max {} min {}", g_settings.spellCostBase,
			g_settings.spellCostScale, g_settings.barterMax, g_settings.barterMin);
	}

	float EffectiveSkill(RE::ActorValue a_av, float a_current) noexcept
	{
		if (a_current <= Curves::kVanillaCap || !Skills::IsSkill(a_av)) {
			return a_current;
		}
		const auto& config = Config::Get();
		switch (Skills::ScalingOf(a_av)) {
		case Skills::Scaling::kSpellCost:
			return static_cast<float>(Curves::SpellCostFormula{ g_settings.spellCostBase, g_settings.spellCostScale }
					.EffectiveSkill(a_current, config.spellCostLimit));
		case Skills::Scaling::kBarter:
			// Before perks the buy/sell ratio is buy^2, so the buy factor
			// heads for the square root of the ratio (FR-26).
			return static_cast<float>(Curves::BarterFormula{ g_settings.barterMax, g_settings.barterMin }
					.EffectiveSkill(a_current, std::sqrt(config.barterMinRatio)));
		case Skills::Scaling::kSqrt:
		default:
			return static_cast<float>(Curves::SqrtEffectiveSkill(a_current));
		}
	}

	void LogSelfCheck()
	{
		for (const auto av : { RE::ActorValue::kOneHanded, RE::ActorValue::kDestruction, RE::ActorValue::kSpeech }) {
			const auto* info = RE::ActorValueList::GetActorValueInfo(av);
			logger::info("Scaling self-check: {} 100 -> {}, 150 -> {}, 400 -> {}",
				info && info->GetFullName() ? info->GetFullName() : "?", EffectiveSkill(av, 100.0f),
				EffectiveSkill(av, 150.0f), EffectiveSkill(av, 400.0f));
		}
	}

	void SetOriginalGetActorValue(std::uintptr_t a_original) noexcept
	{
		g_original = reinterpret_cast<GetActorValue_t>(a_original);
	}

	float GetActorValue(RE::ActorValueOwner* a_this, RE::ActorValue a_av)
	{
		const float value = g_original(a_this, a_av);
		if (value <= Curves::kVanillaCap || g_menuScopes.load(std::memory_order_relaxed) > 0) {
			return value;
		}
		return EffectiveSkill(a_av, value);
	}

	float RealActorValue(RE::ActorValue a_av)
	{
		auto* avo = PlayerAVO();
		if (!avo) {
			return 0.0f;
		}
		return g_original ? g_original(avo, a_av) : avo->GetActorValue(a_av);
	}

	float GetClampedActorValue(RE::ActorValueOwner* a_this, RE::ActorValue a_av)
	{
		// Through the vtable, as the original does: for the player that is our
		// remap, so s_eff is what gets clamped.
		const float value = a_this->GetActorValue(a_av);
		const auto* info = RE::ActorValueList::GetActorValueInfo(a_av);
		if (!info) {
			return value;
		}
		const bool player = a_this == PlayerAVO();
		return static_cast<float>(Curves::ClampActorValue(value, info->flags.underlying(), player));
	}

	void UpdateSkillList(RE::StatsMenu* a_menu)
	{
		static REL::Relocation<void(RE::StatsMenu*)> original{ RELOCATION_ID(51652, 52525) };
		g_menuScopes.fetch_add(1, std::memory_order_relaxed);
		original(a_menu);
		g_menuScopes.fetch_sub(1, std::memory_order_relaxed);
	}
}
