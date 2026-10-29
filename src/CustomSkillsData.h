#pragma once

// Game-free model of the per-skill settings for Custom Skills Framework (CSF)
// skills: how the INI strings of a [CustomSkill.<id>] section are parsed and
// validated. No game or CommonLib dependencies, so this header is unit-tested
// natively on Linux (tests/test_customskills.cpp).

#include "Curves.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace BM::CustomSkillsData
{
	// A form in CSF's "plugin.esp|D63" syntax: the plugin and the form ID
	// within it (hex, no prefix).
	struct FormRef
	{
		std::string   plugin;
		std::uint32_t localID{ 0 };
	};

	enum class PerkSource : std::uint8_t
	{
		kPlayer,  // the player's shared perk points
		kOwn      // the skill's own pool
	};

	struct Settings
	{
		bool       enable{ false };
		PerkSource perkSource{ PerkSource::kPlayer };
	};

	// The values as read from the INI, before any validation.
	struct RawSettings
	{
		std::string enable;
		std::string perkSource;
	};

	struct Parsed
	{
		Settings                 settings;
		std::vector<std::string> warnings;
	};

	namespace detail
	{
		[[nodiscard]] inline std::string Normalize(std::string_view a_text)
		{
			const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
			while (!a_text.empty() && isSpace(static_cast<unsigned char>(a_text.front()))) {
				a_text.remove_prefix(1);
			}
			while (!a_text.empty() && isSpace(static_cast<unsigned char>(a_text.back()))) {
				a_text.remove_suffix(1);
			}
			std::string out(a_text);
			std::ranges::transform(out, out.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}
	}

	// "plugin.esp|D63". Anything without a plugin, or with a missing or
	// non-hex or oversized ID, is not a form reference.
	[[nodiscard]] inline std::optional<FormRef> ParseFormRef(std::string_view a_text)
	{
		const auto bar = a_text.find('|');
		if (bar == std::string_view::npos || bar == 0 || bar + 1 == a_text.size()) {
			return std::nullopt;
		}
		const auto    id = a_text.substr(bar + 1);
		std::uint32_t value = 0;
		const auto    end = id.data() + id.size();
		const auto [ptr, ec] = std::from_chars(id.data(), end, value, 16);
		if (ec != std::errc{} || ptr != end) {
			return std::nullopt;
		}
		return FormRef{ std::string(a_text.substr(0, bar)), value };
	}

	// Validates the raw INI values. A bad value falls back to its default and
	// adds a warning for the log; an empty one is simply unset. a_hasOwnPool:
	// whether the skill has a pool of its own, which "own" needs.
	[[nodiscard]] inline Parsed ParseSettings(const RawSettings& a_raw, bool a_hasOwnPool)
	{
		Parsed out;
		auto&  s = out.settings;
		auto&  warn = out.warnings;

		if (const auto v = detail::Normalize(a_raw.enable); !v.empty()) {
			if (v == "true" || v == "1" || v == "yes" || v == "on") {
				s.enable = true;
			} else if (v != "false" && v != "0" && v != "no" && v != "off") {
				warn.push_back("bEnable \"" + a_raw.enable + "\" is not a boolean, using false");
			}
		}

		if (const auto v = detail::Normalize(a_raw.perkSource); !v.empty() && v != "player") {
			if (v != "own") {
				warn.push_back("sPerkSource \"" + a_raw.perkSource + "\" is not player or own, using player");
			} else if (!a_hasOwnPool) {
				warn.push_back("sPerkSource own needs a skill with its own perk pool, using player");
			} else {
				s.perkSource = PerkSource::kOwn;
			}
		}

		return out;
	}

	[[nodiscard]] inline std::string SectionName(std::string_view a_id)
	{
		return "CustomSkill." + std::string(a_id);
	}

	// The names that exactly one skill has. A name shared by several skills
	// cannot pick one of them, so it is left out.
	[[nodiscard]] inline std::set<std::string> UniqueNames(const std::vector<std::string>& a_names)
	{
		std::set<std::string> seen, repeated;
		for (const auto& name : a_names) {
			if (!seen.insert(name).second) {
				repeated.insert(name);
			}
		}
		std::set<std::string> out;
		std::ranges::set_difference(seen, repeated, std::inserter(out, out.end()));
		return out;
	}

	// A custom skill's cap and the gates paid in its current climb, kept in the
	// co-save under the skill's CSF id (PRD FR-1, FR-13).
	struct CustomCap
	{
		std::uint16_t cap{ 100 };
		std::uint8_t  gatesPaid{ 0 };

		friend bool operator==(const CustomCap&, const CustomCap&) = default;
	};

	// The 'CCAP' record, little-endian: u32 count, then per entry u16 id
	// length, the id bytes, u16 cap, u8 gates. Entries stay even when their
	// mod is gone, so uninstalling and reinstalling it loses nothing. An id
	// over 65535 bytes cannot be written and is skipped.
	[[nodiscard]] inline std::vector<std::byte> EncodeCaps(const std::map<std::string, CustomCap>& a_caps)
	{
		constexpr std::size_t kMaxId = std::numeric_limits<std::uint16_t>::max();

		std::vector<std::byte> out;
		const auto             put = [&out](std::uint64_t a_value, int a_bytes) {
			for (int i = 0; i < a_bytes; ++i) {
				out.push_back(static_cast<std::byte>((a_value >> (8 * i)) & 0xFF));
			}
		};

		std::uint32_t count = 0;
		for (const auto& [id, cap] : a_caps) {
			count += id.size() <= kMaxId ? 1 : 0;
		}
		put(count, 4);
		for (const auto& [id, cap] : a_caps) {
			if (id.size() > kMaxId) {
				continue;
			}
			put(id.size(), 2);
			for (const char c : id) {
				out.push_back(static_cast<std::byte>(c));
			}
			put(cap.cap, 2);
			put(cap.gatesPaid, 1);
		}
		return out;
	}

	// The largest 'CCAP' record that is read. The loader allocates the length
	// from the record header before reading it, so a damaged header could
	// otherwise ask for gigabytes and terminate the game inside SKSE's load
	// callback (FR-22). 1 MiB holds thousands of skills.
	inline constexpr std::uint32_t kMaxCapsRecordLength = 1u << 20;

	[[nodiscard]] constexpr bool IsPlausibleCapsRecordLength(std::uint32_t a_length) noexcept
	{
		return a_length <= kMaxCapsRecordLength;
	}

	// nullopt for a truncated or oversized buffer, or a cap that is not
	// Curves::IsPlausibleCap: damaged data, not a cap.
	[[nodiscard]] inline std::optional<std::map<std::string, CustomCap>> DecodeCaps(std::span<const std::byte> a_bytes)
	{
		std::size_t pos = 0;
		const auto  get = [&](int a_count) -> std::optional<std::uint32_t> {
			if (a_bytes.size() - pos < static_cast<std::size_t>(a_count)) {
				return std::nullopt;
			}
			std::uint32_t value = 0;
			for (int i = 0; i < a_count; ++i) {
				value |= std::to_integer<std::uint32_t>(a_bytes[pos + i]) << (8 * i);
			}
			pos += a_count;
			return value;
		};

		const auto count = get(4);
		if (!count) {
			return std::nullopt;
		}
		std::map<std::string, CustomCap> out;
		for (std::uint32_t n = 0; n < *count; ++n) {
			const auto len = get(2);
			if (!len || a_bytes.size() - pos < *len) {
				return std::nullopt;
			}
			std::string id;
			for (std::uint32_t i = 0; i < *len; ++i) {
				id.push_back(static_cast<char>(a_bytes[pos + i]));
			}
			pos += *len;
			const auto cap = get(2);
			const auto gates = get(1);
			if (!cap || !gates || !Curves::IsPlausibleCap(static_cast<std::uint16_t>(*cap))) {
				return std::nullopt;
			}
			out[id] = CustomCap{ static_cast<std::uint16_t>(*cap), static_cast<std::uint8_t>(*gates) };
		}
		if (pos != a_bytes.size()) {
			return std::nullopt;
		}
		return out;
	}
}
