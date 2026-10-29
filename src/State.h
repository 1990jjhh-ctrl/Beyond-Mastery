#pragma once

#include "Curves.h"
#include "CustomSkillsData.h"
#include "Skills.h"

#include <map>
#include <mutex>

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

		// Custom skills are keyed by their CSF id. An id never seen is at the
		// default; entries of uninstalled mods are kept untouched.
		//
		// CSF asks for a custom skill's limit (CustomSkills::LevelLimit) on
		// whatever thread advances the skill, while the main thread upgrades,
		// loads, reverts and saves, so every access to the map takes
		// _customMutex. The vanilla arrays need none: only the main thread
		// touches them.
		[[nodiscard]] CustomSkillsData::CustomCap CustomCapOf(std::string_view a_id) const
		{
			const std::string     key(a_id);
			const std::lock_guard lock(_customMutex);
			const auto            it = _customCaps.find(key);
			return it == _customCaps.end() ? CustomSkillsData::CustomCap{} : it->second;
		}
		void SetCustomCap(std::string_view a_id, CustomSkillsData::CustomCap a_cap)
		{
			std::string           key(a_id);
			const std::lock_guard lock(_customMutex);
			_customCaps.insert_or_assign(std::move(key), a_cap);
		}
		[[nodiscard]] std::uint16_t CustomLimitOf(std::string_view a_id) const
		{
			const auto c = CustomCapOf(a_id);
			return Curves::Limit(c.cap, c.gatesPaid);
		}

		void Reset()
		{
			_caps.fill(kDefaultCap);
			_gatesPaid.fill(0);
			const std::lock_guard lock(_customMutex);
			_customCaps.clear();
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

		mutable std::mutex                                 _customMutex;
		std::map<std::string, CustomSkillsData::CustomCap> _customCaps;
	};
}
