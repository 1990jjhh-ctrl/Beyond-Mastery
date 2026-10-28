#pragma once

// Formula scaling above 100, for the player only (PRD §6.9, hook-points.md,
// "Formula scaling").
//
// One remap point: slot 1 of the player's ActorValueOwner vtable returns an
// effective skill s_eff for skills above 100, so every formula reading the
// player's skill sees it. The engine's generic skill clamp would cut s_eff
// back to 100, so it is replaced with one that lifts the ceiling for the
// player. The skills menu bypasses the remap while it builds its list
// (FR-29). Hooks.cpp installs all of it; this unit holds the callees.
//
// Every other reader of the player's current skill -- Papyrus, perk
// conditions, other mods -- also sees s_eff, which stays >= 100 above 100, so
// vanilla perk requirements still pass. The engine's XP routine reads the base
// value (vtable slot 3), so leveling never sees s_eff.

namespace BM::Scaling
{
	// Looks up the formula GMSTs once the game data is loaded (kDataLoaded)
	// and logs them; every use then reads the setting live, so mods changing
	// a GMST at runtime are followed. A missing setting keeps the esm value.
	void CacheSettings();

	// s_eff for a_current: identity at or below 100 and for non-skills,
	// otherwise the skill's form (Skills::ScalingOf).
	[[nodiscard]] float EffectiveSkill(RE::ActorValue a_av, float a_current) noexcept;

	// Logs s_eff at 150 and 400 for one skill of each form, so a wrong form
	// or setting shows in the log at load.
	void LogSelfCheck();

	// The engine's GetActorValue the vtable slot held before us. Set before
	// the slot is written, so the callee never runs without it.
	void SetOriginalGetActorValue(std::uintptr_t a_original) noexcept;

	// Callee of the player's vtable slot: the engine's value, remapped.
	float GetActorValue(RE::ActorValueOwner* a_this, RE::ActorValue a_av);

	// The player's current value without the remap.
	[[nodiscard]] float RealActorValue(RE::ActorValue a_av);

	// Replacement for ActorValueOwner::GetClampedActorValue: vanilla clamping
	// for everyone, except that the player's skills are only kept >= 0.
	float GetClampedActorValue(RE::ActorValueOwner* a_this, RE::ActorValue a_av);

	// Call-site thunk for StatsMenu::UpdateSkillList: the original, with the
	// remap off for its duration (FR-29).
	void UpdateSkillList(RE::StatsMenu* a_menu);

	// The engine's charge-per-use function, taken from its call site. Set
	// before the call is redirected.
	void SetOriginalChargePerUse(std::uintptr_t a_original) noexcept;

	// Call-site thunk replacing the engine's charge per use (FR-30): computed
	// from the real Enchanting skill, vanilla up to 100, form B above.
	float ChargePerUse(float a_baseCost, float a_skill);

	// Callee of the barter final-clamp stub (FR-26): the engine's clamp of
	// the buy and sell factors after perks, plus the buy/sell ratio floor.
	void BarterClamp(float* a_buy, float* a_sell);

	// The engine's spell-cost skill factor, taken from its call sites. Set
	// before the calls are redirected.
	void SetOriginalSpellCost(std::uintptr_t a_original) noexcept;

	// Call-site thunk for the spell-cost skill factor (FR-25): the engine's
	// own, fed the skill that makes it compute form B of the real skill.
	float SpellCostFactor(RE::ActorValueOwner* a_caster, float a_cost, std::int32_t a_skill);
}
