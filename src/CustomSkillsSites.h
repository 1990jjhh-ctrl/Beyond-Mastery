#pragma once

// Every patch site in Custom Skills Framework 3.2.0, whose Skill::Advance and
// Skill::Increment stop a custom skill at a hard-coded level 100. The stubs
// that replace them are in Stubs.h (LevelLimitStub, UseXPFactorStub); the
// installer is InstallCustomSkills in Hooks.cpp (FR-35).
//
// A site is an RVA in CustomSkills.dll plus an offset. Which builds are
// supported is PRD FR-35; how the sites were found, and why they are RVAs
// rather than Address Library IDs, is docs/hook-points.md ("Custom Skills
// Framework 3.2.0"). Nothing of CSF's code is copied.
// tests/test_customskills_sites.py checks every pattern against a real
// CustomSkills.dll.
//
// Pattern syntax as in HookSites.h: hex bytes separated by spaces.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace BM::CustomSkillsSites
{
	inline constexpr auto kModule = L"CustomSkills.dll";

	struct Function
	{
		std::string_view name;
		std::uint32_t    rva;
		std::string_view entry;  // the function's first bytes, matched before any site in it
	};

	struct Site
	{
		std::string_view name;
		const Function*  function;
		std::uint32_t    offset;  // from the function's entry
		std::string_view pattern;
		std::size_t      length;  // bytes replaced: a 5-byte jmp or call, then NOPs
	};

	// Skill::Advance(float, TESForm*, bool a_isSkillUse, bool). `this` is in
	// r14, the level in esi (both callee-saved), and Level, the skill's level
	// global, is the TESGlobal* at [this + 0x40].
	inline constexpr Function kAdvance{
		"Skill::Advance",
		0x4CAE0,
		"48 8B C4 48 89 58 18 55 56 57 41 56 41 57"
	};

	// Skill::Increment(std::uint32_t a_count). `this` is in rdi, the level in
	// r14d (both callee-saved), and a_count stays live in edx.
	inline constexpr Function kIncrement{
		"Skill::Increment",
		0x4D1D0,
		"85 D2 0F 84"
	};

	// Where the replaced branches went, from each function's entry.
	inline constexpr std::uint32_t kAdvanceLoopTop = 0x1B0;   // the level-up loop's body
	inline constexpr std::uint32_t kAdvanceLoopExit = 0x38B;  // stores the level and ratio
	inline constexpr std::uint32_t kIncrementReturn = 0x5B9;  // the epilogue

	// The skill's level global, at [this + 0x40] in both functions.
	inline constexpr std::uint32_t kLevelMember = 0x40;

	// `call fmaf` computing the use XP, magnitude x use mult + use offset. It
	// runs only when a_isSkillUse is true. The pattern adds the store of the
	// result that follows, which both paths share, so it is not patched.
	inline constexpr Site kAdvanceUseXP{
		"Skill::Advance use XP",
		&kAdvance,
		0xB4,
		"E8 87 C1 06 00 F3 0F 11 84 24 80 00 00 00",
		5
	};

	// `cmp esi, 0x64; jge kAdvanceLoopExit`: the loop is not entered at 100.
	inline constexpr Site kAdvanceLoopEntry{
		"Skill::Advance loop entry",
		&kAdvance,
		0x17B,
		"83 FE 64 0F 8D 07 02 00 00",
		9
	};

	// `cmp esi, 0x64; jl kAdvanceLoopTop`: the loop ends at 100.
	inline constexpr Site kAdvanceLoopRepeat{
		"Skill::Advance loop repeat",
		&kAdvance,
		0x382,
		"83 FE 64 0F 8C 25 FE FF FF",
		9
	};

	// `cmp r14d, 0x64; jge kIncrementReturn`: nothing to add at 100.
	inline constexpr Site kIncrementCheck{
		"Skill::Increment check",
		&kIncrement,
		0x8C,
		"41 83 FE 64 0F 8D 23 05 00 00",
		10
	};

	// `mov eax, 0x64; sub eax, r14d`: the levels left, which the following
	// `cmp eax, edx` clamps a_count to (the sub's flags are dead).
	inline constexpr Site kIncrementCount{
		"Skill::Increment count",
		&kIncrement,
		0x96,
		"B8 64 00 00 00 41 2B C6",
		8
	};

	inline constexpr std::array kFunctions{ &kAdvance, &kIncrement };

	// The four sites that lift the limit; kAdvanceUseXP is installed only
	// with bScaleXPGainAbove100.
	inline constexpr std::array kLimitSites{ &kAdvanceLoopEntry, &kAdvanceLoopRepeat, &kIncrementCheck,
		&kIncrementCount };
}
