#pragma once

// Every patch site in the supported Custom Skills Framework builds, whose
// Skill::Advance and Skill::Increment stop a custom skill at a hard-coded
// level 100. The stubs that replace them are in Stubs.h (LevelLimitStub,
// UseXPFactorStub); the installer is InstallCustomSkills in Hooks.cpp, which
// patches the build whose function entries match (FR-35).
//
// A site is an RVA in CustomSkills.dll plus an offset. Which builds are
// supported is PRD FR-35; how the sites were found, and why they are RVAs
// rather than Address Library IDs, is docs/hook-points.md ("Custom Skills
// Framework"). Nothing of CSF's code is copied.
// tests/test_customskills_sites.py checks every pattern against real
// CustomSkills.dll files.
//
// Pattern syntax as in HookSites.h: hex bytes separated by spaces.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace BM::CustomSkillsSites
{
inline constexpr auto kModule = L"CustomSkills.dll";

// A callee-saved register a stub reads. The 64-bit register holds `this`;
// its low 32 bits (esi, edi, r14d) hold the level.
enum class Reg : std::uint8_t
{
	kRsi,
	kRdi,
	kR14
};

struct Function
{
	std::string_view name;
	std::uint32_t    rva;
	std::string_view entry;  // the function's first bytes, matched before any site in it
	Reg              self;   // holds `this` at every site in the function
	Reg              level;  // holds the skill's level at every site
};

struct Site
{
	std::string_view name;
	const Function*  function;
	std::uint32_t    offset;  // from the function's entry
	std::string_view pattern;
	std::size_t      length;  // bytes replaced: a 5-byte jmp or call, then NOPs
};

// One supported build: its two functions, its five sites, and where the
// replaced branches went, from each function's entry.
struct Build
{
	std::string_view version;
	const Function*  advance;
	const Function*  increment;
	const Site*      useXP;            // `call fmaf`, the use XP; only with bScaleXPGainAbove100
	const Site*      loopEntry;        // `cmp level, 100; jge loopExit`: not entered at 100
	const Site*      loopRepeat;       // `cmp level, 100; jl loopTop`: the loop ends at 100
	const Site*      incrementCheck;   // `cmp level, 100; jge incrementReturn`: nothing to add
	const Site*      incrementCount;   // `mov eax, 100; sub eax, level`: the levels left
	std::uint32_t    loopTop;          // in Advance: the level-up loop's body
	std::uint32_t    loopExit;         // in Advance: stores the level and ratio
	std::uint32_t    incrementReturn;  // in Increment: the epilogue
};

// The skill's level global, at [this + 0x40] in both functions of every
// supported build.
inline constexpr std::uint32_t kLevelMember = 0x40;

// --- 3.2.0 ------------------------------------------------------------------

// Skill::Advance(float, TESForm*, bool a_isSkillUse, bool).
inline constexpr Function kAdvance320{
	"Skill::Advance",
	0x4CAE0,
	"48 8B C4 48 89 58 18 55 56 57 41 56 41 57",
	Reg::kR14,
	Reg::kRsi
};

// Skill::Increment(std::uint32_t a_count); a_count stays live in edx.
inline constexpr Function kIncrement320{
	"Skill::Increment",
	0x4D1D0,
	"85 D2 0F 84",
	Reg::kRdi,
	Reg::kR14
};

inline constexpr std::uint32_t kAdvanceLoopTop320 = 0x1B0;
inline constexpr std::uint32_t kAdvanceLoopExit320 = 0x38B;
inline constexpr std::uint32_t kIncrementReturn320 = 0x5B9;

// `call fmaf` computing the use XP, magnitude x use mult + use offset. It
// runs only when a_isSkillUse is true. The pattern adds the store of the
// result that follows, which both paths share, so it is not patched.
inline constexpr Site kAdvanceUseXP320{
	"Skill::Advance use XP",
	&kAdvance320,
	0xB4,
	"E8 87 C1 06 00 F3 0F 11 84 24 80 00 00 00",
	5
};

inline constexpr Site kAdvanceLoopEntry320{
	"Skill::Advance loop entry",
	&kAdvance320,
	0x17B,
	"83 FE 64 0F 8D 07 02 00 00",
	9
};

inline constexpr Site kAdvanceLoopRepeat320{
	"Skill::Advance loop repeat",
	&kAdvance320,
	0x382,
	"83 FE 64 0F 8C 25 FE FF FF",
	9
};

inline constexpr Site kIncrementCheck320{
	"Skill::Increment check",
	&kIncrement320,
	0x8C,
	"41 83 FE 64 0F 8D 23 05 00 00",
	10
};

// The following `cmp eax, edx` clamps a_count to the levels left (the sub's
// flags are dead).
inline constexpr Site kIncrementCount320{
	"Skill::Increment count",
	&kIncrement320,
	0x96,
	"B8 64 00 00 00 41 2B C6",
	8
};

inline constexpr Build kBuild320{ "3.2.0", &kAdvance320, &kIncrement320, &kAdvanceUseXP320, &kAdvanceLoopEntry320,
	&kAdvanceLoopRepeat320, &kIncrementCheck320, &kIncrementCount320, kAdvanceLoopTop320, kAdvanceLoopExit320,
	kIncrementReturn320 };

// --- 3.1.0 ------------------------------------------------------------------
// The same code compiled with other registers and offsets.

inline constexpr Function kAdvance310{
	"Skill::Advance",
	0x40070,
	"48 89 74 24 20 57 48 81 EC F0 00 00 00",
	Reg::kRsi,
	Reg::kRdi
};

// a_count stays live in edx, as in 3.2.0; r14d holds a copy.
inline constexpr Function kIncrement310{
	"Skill::Increment",
	0x40600,
	"85 D2 0F 84",
	Reg::kRdi,
	Reg::kRsi
};

inline constexpr std::uint32_t kAdvanceLoopTop310 = 0x160;
inline constexpr std::uint32_t kAdvanceLoopExit310 = 0x30A;
inline constexpr std::uint32_t kIncrementReturn310 = 0x37E;

// As in 3.2.0, a `je` past the call keeps it to skill use.
inline constexpr Site kAdvanceUseXP310{
	"Skill::Advance use XP",
	&kAdvance310,
	0xAA,
	"E8 29 C0 03 00 F3 0F 11 44 24 20",
	5
};

inline constexpr Site kAdvanceLoopEntry310{
	"Skill::Advance loop entry",
	&kAdvance310,
	0x10F,
	"83 FF 64 0F 8D F2 01 00 00",
	9
};

inline constexpr Site kAdvanceLoopRepeat310{
	"Skill::Advance loop repeat",
	&kAdvance310,
	0x2E6,
	"83 FF 64 0F 8C 71 FE FF FF",
	9
};

inline constexpr Site kIncrementCheck310{
	"Skill::Increment check",
	&kIncrement310,
	0x73,
	"83 FE 64 0F 8D 02 03 00 00",
	9
};

// r11, used after this site, is volatile: the stub saves it.
inline constexpr Site kIncrementCount310{
	"Skill::Increment count",
	&kIncrement310,
	0x80,
	"B8 64 00 00 00 2B C6",
	7
};

inline constexpr Build kBuild310{ "3.1.0", &kAdvance310, &kIncrement310, &kAdvanceUseXP310, &kAdvanceLoopEntry310,
	&kAdvanceLoopRepeat310, &kIncrementCheck310, &kIncrementCount310, kAdvanceLoopTop310, kAdvanceLoopExit310,
	kIncrementReturn310 };

// Newest first; the installer takes the first whose function entries match.
inline constexpr std::array kBuilds{ &kBuild320, &kBuild310 };
}
