#pragma once

// The 18 player skills in PlayerSkills order. The skill actor values are
// contiguous: ActorValue::kOneHanded (6) ... ActorValue::kEnchanting (23).

namespace BM::Skills
{
	inline constexpr std::size_t kCount = 18;
	inline constexpr auto        kFirstAV = static_cast<std::uint32_t>(RE::ActorValue::kOneHanded);
	inline constexpr auto        kLastAV = static_cast<std::uint32_t>(RE::ActorValue::kEnchanting);
	static_assert(kLastAV - kFirstAV + 1 == kCount);

	[[nodiscard]] inline bool IsSkill(RE::ActorValue a_av) noexcept
	{
		const auto v = static_cast<std::uint32_t>(a_av);
		return v >= kFirstAV && v <= kLastAV;
	}

	[[nodiscard]] inline std::size_t ToIndex(RE::ActorValue a_av) noexcept
	{
		return static_cast<std::uint32_t>(a_av) - kFirstAV;
	}

	[[nodiscard]] inline RE::ActorValue ToAV(std::size_t a_index) noexcept
	{
		return static_cast<RE::ActorValue>(kFirstAV + a_index);
	}

	// How the remap continues this skill above 100 (PRD §6.9). Form A for all
	// but Speech: spell cost gets form B in its own calls, from the real skill
	// recovered from form A, so it holds for any skill an effect is tied to.
	enum class Scaling : std::uint8_t
	{
		kSqrt,   // form A
		kBarter  // form B through the inverse of the barter formula (hook-points.md §6)
	};

	[[nodiscard]] inline Scaling ScalingOf(RE::ActorValue a_av) noexcept
	{
		using AV = RE::ActorValue;
		switch (a_av) {
		case AV::kSpeech:
			return Scaling::kBarter;
		default:
			return Scaling::kSqrt;
		}
	}
}
