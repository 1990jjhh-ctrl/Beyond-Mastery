#pragma once

// Game-free model of the per-skill settings for Custom Skills Framework (CSF)
// skills: how the INI strings of a [CustomSkill.<id>] section are parsed and
// validated. No game or CommonLib dependencies, so this header is unit-tested
// natively on Linux (tests/test_customskills.cpp).

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <optional>
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

}
