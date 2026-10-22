#pragma once

// Byte-signature matching for the hook installer. Every patch site is
// compared against its expected instruction bytes before anything is written,
// so a site another plugin already patched is skipped instead of corrupted
// (docs/hook-points.md "Conventions", FR-20).
//
// Game-free and header-only, like Curves.h, so it is unit-tested natively
// (tests/test_patterns.cpp) rather than only through the cross-compiled DLL.
//
// Pattern syntax matches src/HookSites.h: hex byte pairs separated by spaces,
// "??" for any byte. An empty pattern covers zero bytes and matches anything,
// which is how a whole-function hook says "no byte check here".

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace BM::Patterns
{
	namespace detail
	{
		[[nodiscard]] constexpr int HexValue(char a_c) noexcept
		{
			if (a_c >= '0' && a_c <= '9') {
				return a_c - '0';
			}
			if (a_c >= 'a' && a_c <= 'f') {
				return a_c - 'a' + 10;
			}
			if (a_c >= 'A' && a_c <= 'F') {
				return a_c - 'A' + 10;
			}
			return -1;
		}

		// Index of the next token start at or after a_i, skipping spaces.
		[[nodiscard]] constexpr std::size_t SkipSpaces(std::string_view a_p, std::size_t a_i) noexcept
		{
			while (a_i < a_p.size() && a_p[a_i] == ' ') {
				++a_i;
			}
			return a_i;
		}

		[[nodiscard]] constexpr std::size_t EndOfToken(std::string_view a_p, std::size_t a_i) noexcept
		{
			while (a_i < a_p.size() && a_p[a_i] != ' ') {
				++a_i;
			}
			return a_i;
		}
	}

	// Bytes the pattern covers. 0 for an empty pattern.
	[[nodiscard]] constexpr std::size_t Length(std::string_view a_pattern) noexcept
	{
		std::size_t count = 0;
		for (std::size_t i = detail::SkipSpaces(a_pattern, 0); i < a_pattern.size();
			i = detail::SkipSpaces(a_pattern, detail::EndOfToken(a_pattern, i))) {
			++count;
		}
		return count;
	}

	// True when the Length(a_pattern) bytes at a_at satisfy the pattern.
	//
	// a_at must be readable for Length(a_pattern) bytes: at a patch site that
	// is guaranteed by the address library resolving inside the game's .text.
	// A malformed pattern never matches, so a typo disables its site rather
	// than letting a wrong patch through.
	[[nodiscard]] constexpr bool Matches(std::string_view a_pattern, const std::uint8_t* a_at) noexcept
	{
		if (a_pattern.empty()) {
			return true;
		}
		if (a_at == nullptr) {
			return false;
		}

		std::size_t byte = 0;
		for (std::size_t i = detail::SkipSpaces(a_pattern, 0); i < a_pattern.size();
			i = detail::SkipSpaces(a_pattern, detail::EndOfToken(a_pattern, i)), ++byte) {
			const auto end = detail::EndOfToken(a_pattern, i);
			if (end - i != 2) {
				return false;  // malformed token
			}
			if (a_pattern[i] == '?' && a_pattern[i + 1] == '?') {
				continue;  // wildcard
			}
			const int hi = detail::HexValue(a_pattern[i]);
			const int lo = detail::HexValue(a_pattern[i + 1]);
			if (hi < 0 || lo < 0) {
				return false;  // malformed token
			}
			if (a_at[byte] != static_cast<std::uint8_t>(hi * 16 + lo)) {
				return false;
			}
		}
		return true;
	}

	// Bytes that satisfy the pattern, with every wildcard set to a_fill.
	// Test support: it lets a pattern be checked against itself, so a typo in
	// the site table is caught natively instead of in-game.
	[[nodiscard]] inline std::vector<std::uint8_t> Synthesise(std::string_view a_pattern, std::uint8_t a_fill)
	{
		std::vector<std::uint8_t> out;
		out.reserve(Length(a_pattern));
		for (std::size_t i = detail::SkipSpaces(a_pattern, 0); i < a_pattern.size();
			i = detail::SkipSpaces(a_pattern, detail::EndOfToken(a_pattern, i))) {
			const auto end = detail::EndOfToken(a_pattern, i);
			const int  hi = (end - i == 2) ? detail::HexValue(a_pattern[i]) : -1;
			const int  lo = (end - i == 2) ? detail::HexValue(a_pattern[i + 1]) : -1;
			out.push_back((hi < 0 || lo < 0) ? a_fill : static_cast<std::uint8_t>(hi * 16 + lo));
		}
		return out;
	}
}
