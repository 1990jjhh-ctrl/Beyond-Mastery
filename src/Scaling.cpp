#include "Scaling.h"

#include "Config.h"
#include "Curves.h"
#include "Skills.h"

#include <atomic>

namespace BM::Scaling
{
	namespace
	{
		// A formula GMST, looked up once at kDataLoaded and read live on every
		// use, as the engine's own code does: barter and economy mods change
		// these at runtime. The fallback is Skyrim.esm's value, used only for a
		// setting that does not exist.
		struct LiveSetting
		{
			const char*  name;
			double       fallback;
			RE::Setting* setting{ nullptr };

			[[nodiscard]] double Get() const noexcept { return setting ? setting->GetFloat() : fallback; }
		};

		struct Settings
		{
			LiveSetting spellCostBase{ "fMagicCasterPCSkillCostBase", 0.0025 };
			LiveSetting spellCostScale{ "fMagicPCSkillCostScale", 0.65 };
			LiveSetting barterMax{ "fBarterMax", 3.3 };
			LiveSetting barterMin{ "fBarterMin", 2.0 };
			LiveSetting enchantBase{ "fEnchantingSkillCostBase", 0.005 };
			LiveSetting enchantScale{ "fEnchantingSkillCostScale", 0.5 };
			LiveSetting minBuyMult{ "fMinBuyMult", 1.0 };
			LiveSetting maxSellMult{ "fMaxSellMult", 1.0 };

			[[nodiscard]] auto All() noexcept
			{
				return std::array{ &spellCostBase, &spellCostScale, &barterMax, &barterMin, &enchantBase,
					&enchantScale, &minBuyMult, &maxSellMult };
			}
		};

		Settings g_settings;

		// The engine functions our callees forward to. Stored with release
		// before the patch that makes the callee reachable is written, and
		// loaded with acquire on every call, so a thread that reaches the
		// callee always sees them. On x64 both are plain movs.
		using GetActorValue_t = float (*)(RE::ActorValueOwner*, RE::ActorValue);
		std::atomic<GetActorValue_t> g_original{ nullptr };

		// The engine's charge-per-use function (26494), taken from the call
		// site at install: (1 - (skill * base)^scale) * mult * cost^exponent.
		using ChargePerUse_t = float (*)(float, float);
		std::atomic<ChargePerUse_t> g_charge{ nullptr };

		// The engine's spell-cost skill factor (26505): caster, cost, int skill.
		using SpellCost_t = float (*)(RE::ActorValueOwner*, float, std::int32_t);
		std::atomic<SpellCost_t> g_spellCost{ nullptr };

		// Open skills-menu scopes on this thread. StatsMenu::UpdateSkillList
		// runs synchronously on the UI thread and reads the skills itself, so
		// the remap is off for exactly that thread and nowhere else (FR-29).
		thread_local int t_menuScopes = 0;

		[[nodiscard]] Curves::PowerDecayFormula EnchantCharge() noexcept
		{
			return Curves::EnchantChargeFormula(g_settings.enchantBase.Get(), g_settings.enchantScale.Get());
		}

		[[nodiscard]] double EnchantChargeLimit() noexcept
		{
			return Config::Get().enchantChargeLimitFraction * EnchantCharge().Vanilla(Curves::kVanillaCap);
		}

		[[nodiscard]] RE::ActorValueOwner* PlayerAVO()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			return player ? player->AsActorValueOwner() : nullptr;
		}
	}

	void CacheSettings()
	{
		auto* collection = RE::GameSettingCollection::GetSingleton();
		for (auto* live : g_settings.All()) {
			live->setting = collection ? collection->GetSetting(live->name) : nullptr;
			if (live->setting) {
				logger::info("Scaling setting {} = {}", live->name, live->Get());
			} else {
				logger::warn("{} not found; using {}", live->name, live->fallback);
			}
		}
	}

	float EffectiveSkill(RE::ActorValue a_av, float a_current) noexcept
	{
		if (a_current <= Curves::kVanillaCap || !Skills::IsSkill(a_av)) {
			return a_current;
		}
		const auto& config = Config::Get();
		switch (Skills::ScalingOf(a_av)) {
		case Skills::Scaling::kSpellCost:
			return static_cast<float>(Curves::SpellCostFormula{ g_settings.spellCostBase.Get(), g_settings.spellCostScale.Get() }
					.EffectiveSkill(a_current, config.spellCostLimit));
		case Skills::Scaling::kBarter:
			// Before perks the buy/sell ratio is buy^2, so the buy factor
			// heads for the square root of the ratio (FR-26).
			return static_cast<float>(Curves::BarterFormula{ g_settings.barterMax.Get(), g_settings.barterMin.Get() }
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
		const auto charge = EnchantCharge();
		logger::info("Scaling self-check: enchanting charge factor 100 -> {}, 200 -> {}, 1000 -> {}",
			charge.Factor(100.0, EnchantChargeLimit()), charge.Factor(200.0, EnchantChargeLimit()),
			charge.Factor(1000.0, EnchantChargeLimit()));
	}

	void SetOriginalGetActorValue(std::uintptr_t a_original) noexcept
	{
		g_original.store(reinterpret_cast<GetActorValue_t>(a_original), std::memory_order_release);
	}

	float GetActorValue(RE::ActorValueOwner* a_this, RE::ActorValue a_av)
	{
		const float value = g_original.load(std::memory_order_acquire)(a_this, a_av);
		// !(value > 100) rather than value <= 100: NaN fails both, and is
		// passed through untouched, as the engine would return it.
		if (!(value > Curves::kVanillaCap) || t_menuScopes > 0) {
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
		const auto original = g_original.load(std::memory_order_acquire);
		return original ? original(avo, a_av) : avo->GetActorValue(a_av);
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
		++t_menuScopes;
		original(a_menu);
		--t_menuScopes;
	}

	void SetOriginalChargePerUse(std::uintptr_t a_original) noexcept
	{
		g_charge.store(reinterpret_cast<ChargePerUse_t>(a_original), std::memory_order_release);
	}

	float ChargePerUse(float a_baseCost, float)
	{
		// The engine passes GetActorValue(Enchanting), which is s_eff; FR-30
		// wants the real skill. Vanilla up to 100; above it the skill-free part
		// (the engine's own function at skill 0) times form B.
		const auto charge = g_charge.load(std::memory_order_acquire);
		const auto real = RealActorValue(RE::ActorValue::kEnchanting);
		if (!(real > Curves::kVanillaCap)) {
			return charge(a_baseCost, real);  // vanilla, NaN included
		}
		return static_cast<float>(charge(a_baseCost, 0.0f) * EnchantCharge().Factor(real, EnchantChargeLimit()));
	}

	void BarterClamp(float* a_buy, float* a_sell)
	{
		// The engine's own final clamp, then the floor FR-26 adds: the buy
		// price stays at least fBarterMinRatio times the sell price, whatever
		// perks and Fortify Barter did before.
		const auto prices = Curves::BarterFinalClamp(*a_buy, *a_sell, g_settings.minBuyMult.Get(), g_settings.maxSellMult.Get(),
			Config::Get().barterMinRatio);
		*a_buy = static_cast<float>(prices.buy);
		*a_sell = static_cast<float>(prices.sell);
	}

	void SetOriginalSpellCost(std::uintptr_t a_original) noexcept
	{
		g_spellCost.store(reinterpret_cast<SpellCost_t>(a_original), std::memory_order_release);
	}

	float SpellCostFactor(RE::ActorValueOwner* a_caster, float a_cost, std::int32_t a_skill)
	{
		// The magic schools already stay below the ceiling through the remap;
		// this holds the same limit for an effect tied to any other skill.
		// NPC skills are clamped to 100, below any ceiling, so they are vanilla.
		const auto ceiling = Curves::SpellCostFormula{ g_settings.spellCostBase.Get(), g_settings.spellCostScale.Get() }
		                         .SkillCeiling(Config::Get().spellCostLimit);
		return g_spellCost.load(std::memory_order_acquire)(a_caster, a_cost, std::min(a_skill, ceiling));
	}
}
