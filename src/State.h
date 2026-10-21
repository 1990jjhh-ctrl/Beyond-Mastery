#pragma once

#include "Skills.h"

// Per-save state: the cap of each skill (PRD FR-1, FR-22).
// Persisted in the SKSE co-save; missing data = all caps 100.

namespace BM
{
	class State
	{
	public:
		static constexpr std::uint16_t kDefaultCap = 100;

		static State& Get() noexcept;

		[[nodiscard]] std::uint16_t Cap(std::size_t a_skill) const noexcept { return _caps[a_skill]; }

		// Cap for a skill actor value; the vanilla cap for anything that is not
		// a skill, so a non-skill actor value can never widen a limit.
		[[nodiscard]] std::uint16_t CapOf(RE::ActorValue a_av) const noexcept
		{
			return Skills::IsSkill(a_av) ? _caps[Skills::ToIndex(a_av)] : kDefaultCap;
		}
		void                        SetCap(std::size_t a_skill, std::uint16_t a_cap) noexcept { _caps[a_skill] = a_cap; }

		void Reset() noexcept { _caps.fill(kDefaultCap); }

		// Registers save/load/revert callbacks with the SKSE serialization interface.
		static void RegisterSerialization();

	private:
		State() { Reset(); }

		static void OnSave(SKSE::SerializationInterface* a_intfc);
		static void OnLoad(SKSE::SerializationInterface* a_intfc);
		static void OnRevert(SKSE::SerializationInterface* a_intfc);

		std::array<std::uint16_t, Skills::kCount> _caps{};
	};
}
