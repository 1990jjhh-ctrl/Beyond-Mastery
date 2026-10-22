#pragma once

#include "Curves.h"
#include "Skills.h"

// Per-save state: each skill's cap and the gates paid during its current
// climb (PRD FR-1, FR-13, FR-22). Persisted in the SKSE co-save; missing data
// means all caps 100, and gates enough to reach whatever cap was loaded.

namespace BM
{
	class State
	{
	public:
		static constexpr std::uint16_t kDefaultCap = 100;

		static State& Get() noexcept;

		[[nodiscard]] std::uint16_t Cap(std::size_t a_skill) const noexcept { return _caps[a_skill]; }
		void                        SetCap(std::size_t a_skill, std::uint16_t a_cap) noexcept { _caps[a_skill] = a_cap; }

		[[nodiscard]] std::uint8_t GatesPaid(std::size_t a_skill) const noexcept { return _gatesPaid[a_skill]; }
		void                       SetGatesPaid(std::size_t a_skill, std::uint8_t a_gates) noexcept { _gatesPaid[a_skill] = a_gates; }

		// Cap for a skill actor value; the vanilla cap for anything that is not
		// a skill, so a non-skill actor value can never widen a limit.
		[[nodiscard]] std::uint16_t CapOf(RE::ActorValue a_av) const noexcept
		{
			return Skills::IsSkill(a_av) ? _caps[Skills::ToIndex(a_av)] : kDefaultCap;
		}

		// The level a skill can reach right now (PRD §5). This, not the cap, is
		// what the XP routine enforces: after Reset Skill a skill stops at each
		// unpaid gate even though its cap lies higher.
		[[nodiscard]] std::uint16_t LimitOf(RE::ActorValue a_av) const noexcept
		{
			if (!Skills::IsSkill(a_av)) {
				return kDefaultCap;
			}
			const auto i = Skills::ToIndex(a_av);
			return Curves::Limit(_caps[i], _gatesPaid[i]);
		}

		void ApplyUpgrade(std::size_t a_skill, const Curves::Upgrade& a_upgrade) noexcept
		{
			_caps[a_skill] = a_upgrade.newCap;
			_gatesPaid[a_skill] = a_upgrade.newGatesPaid;
		}

		// Reset Skill starts a new climb: the cap stays, the gates are owed again
		// (FR-12, FR-13).
		void ClearGates(std::size_t a_skill) noexcept { _gatesPaid[a_skill] = 0; }

		void Reset() noexcept
		{
			_caps.fill(kDefaultCap);
			_gatesPaid.fill(0);
		}

		// Registers save/load/revert callbacks with the SKSE serialization interface.
		static void RegisterSerialization();

	private:
		State() { Reset(); }

		static void OnSave(SKSE::SerializationInterface* a_intfc);
		static void OnLoad(SKSE::SerializationInterface* a_intfc);
		static void OnRevert(SKSE::SerializationInterface* a_intfc);

		std::array<std::uint16_t, Skills::kCount> _caps{};
		std::array<std::uint8_t, Skills::kCount>  _gatesPaid{};
	};
}
