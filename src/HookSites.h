#pragma once

// Single source of truth for engine addresses (docs/hook-points.md).
//
// Every entry is an Address Library ID plus byte offset for SE 1.5.97 and
// AE 1.6.x, and the instruction bytes expected at the patch site. The hook
// installer compares the bytes before writing and skips the patch on
// mismatch (FR-20: a mismatch usually means another plugin patched the site).
//
// Source: Skyrim AE Uncapper (Rust), commit of 2026-09-07. Only the facts
// about the game binary are taken (IDs, offsets, instruction bytes), no code.
//
// "verified" is set once a site has been confirmed in our own disassembly.
// Every site is confirmed with scripts/verify_sites.py against SkyrimSE.exe
// 1.7.104.0: at drift 0 with its own address library, and through a
// region-local drift with the 1.7.99 one (hook-points.md §9a). At runtime the
// player's own address library supplies the exact offsets.
//
// The AE ids, byte offsets and patterns hold unchanged on 1.7.x, so there is
// no separate 1.7 column: REL::RelocationID picks the AE id for every
// runtime at or above 1.6.317.
//
// Pattern syntax: hex bytes separated by spaces, "??" = any byte.

#include <array>
#include <cstdint>
#include <string_view>

namespace BM::HookSites
{
	// Target runtimes (PRD §4): SE 1.5.97, AE 1.6.x and 1.7.x. Plain aggregate
	// rather than REL::Version so the gate is unit-tested natively.
	struct RuntimeVersion
	{
		std::uint16_t major{};
		std::uint16_t minor{};
		std::uint16_t patch{};
		std::uint16_t build{};

		friend auto operator<=>(const RuntimeVersion&, const RuntimeVersion&) = default;
		friend bool operator==(const RuntimeVersion&, const RuntimeVersion&) = default;
	};

	// Oldest targeted build. Everything below it (VR 1.4.15, LE, older SE
	// builds) is out of scope per PRD §3.
	inline constexpr RuntimeVersion kMinRuntime{ 1, 5, 97, 0 };

	// Newest build whose sites we verified ourselves; see the note above.
	inline constexpr RuntimeVersion kMaxKnownRuntime{ 1, 7, 104, 0 };

	[[nodiscard]] inline bool IsSupportedRuntime(RuntimeVersion a_v) noexcept
	{
		return a_v >= kMinRuntime;
	}

	// A supported runtime newer than anything we verified still gets hooks --
	// the address library resolves the ids -- but the log says so.
	[[nodiscard]] inline bool IsKnownRuntime(RuntimeVersion a_v) noexcept
	{
		return a_v <= kMaxKnownRuntime;
	}

	struct Location
	{
		std::uint64_t id;
		std::uint32_t offset;
	};

	struct Site
	{
		std::string_view name;
		Location         se;
		Location         ae;
		std::string_view patternSE;  // empty = whole-function hook, no byte check here
		std::string_view patternAE;
		bool             verified;
	};

	// Whether a_site has an address on the SE (false) or AE/1.7.x (true)
	// runtime. An id of 0 means the site was never located there, which is
	// expected and distinct from an id the address library cannot resolve.
	[[nodiscard]] constexpr bool ExistsOn(const Site& a_site, bool a_ae) noexcept
	{
		return (a_ae ? a_site.ae.id : a_site.se.id) != 0;
	}

	// --- Skill cap and XP (hook-points.md §1-3) ----------------------------

	// Register holding the skill at the patch point, from our disassembly of
	// 1.7.104 (function entry 0x78dcb0, site 0x78dd1f):
	//
	//   mov esi, edx        ; ActorValue arrives in edx and is saved to esi
	//   lea eax, [rdx-6]
	//   cmp eax, 0x11       ; engine's own range check: valid AVs are 6..23,
	//   ja  <bail>          ; exactly the 18 skills in Skills.h
	//   ... call [rax+0x18] ; <- site: returns the current level in xmm0
	//
	// So at the site the ActorValue is in **esi**, and esi is callee-saved, so
	// it survives the call the patch sits on -- edx would not. The cap value
	// belongs in xmm10 for AE/1.7.x.
	//
	// SE 1.5.97 is NOT established: its pattern shows a different shape
	// (movss xmm0 then movaps xmm6, so the cap lands in xmm6), and we have no
	// 1.5.97 binary to disassemble. Do not patch the SE site until it is.
	inline constexpr Site kGetSkillCap{
		"GetSkillCap",
		{ 40554, 0x45 },
		{ 41561, 0x6F },
		"FF 50 18 F3 44 0F 10 05 ?? ?? ?? ?? 0F 28 F0",
		"FF 50 18 44 0F 28 C0 F3 44 0F 10 15 ?? ?? ?? ??",
		true
	};

	inline constexpr Site kImprovePlayerSkillPoints{
		"ImprovePlayerSkillPoints",
		{ 40554, 0xDC },
		{ 41561, 0xF1 },
		"F3 0F 10 44 24 30 F3 0F 59 C7 F3 0F 58 44 24 34",
		"F3 0F 10 44 24 30 F3 0F 59 C6 F3 0F 58 44 24 34",
		true
	};

	// Installed only with bClampCharXPAbove100 = true (FR-11).
	inline constexpr Site kImproveLevelExpBySkillLevel{
		"ImproveLevelExpBySkillLevel",
		{ 40576, 0x70 },
		{ 41561, 0x2CB },
		"F3 0F 5C C1 F3 0F 59 05 ?? ?? ?? ??",
		"F3 0F 5C CA F3 0F 59 0D ?? ?? ?? ??",
		true
	};

	// --- Effective skill (hook-points.md §5.1, §5.2) -------------------------

	// Whole-function hook on the player's ActorValueOwner::GetActorValue.
	inline constexpr Site kPlayerAVOGetCurrent{
		"PlayerAVOGetCurrent",
		{ 37517, 0 },
		{ 38462, 0 },
		"40 55 56 57 41 56 41 57 48 83 EC 40",
		"4C 8B DC 55 56 57 41 56 41 57 48 83 EC 50",
		true
	};

	// Skills menu list update, StatsMenu::UpdateSkillList(StatsMenu*). Not
	// patched itself: its three direct calls are, so the remap is bypassed for
	// exactly its duration (FR-29), and each call must still target it.
	inline constexpr Site kUpdateSkillList{
		"UpdateSkillList",
		{ 51652, 0 },
		{ 52525, 0 },
		"48 8B C4 55 53 56 57 41 54 41 55 41 56",
		"48 8B C4 55 53 56 57 41 54 41 55 41 56",
		true
	};

	// mov rcx, <menu>; call UpdateSkillList, plus what follows until each
	// pattern is unique within the verifier's drift window. AE/1.7.x only.
	inline constexpr std::uint32_t kSkillListCallOffsetA = 3;
	inline constexpr std::uint32_t kSkillListCallOffsetB = 3;
	inline constexpr std::uint32_t kSkillListCallOffsetC = 4;

	inline constexpr Site kSkillListCallA{ "SkillListCallA", { 0, 0 }, { 52510, 0x950 }, "", "48 8B CE E8 ?? ?? ?? ?? 48 8B CE E8 ?? ?? ?? ?? 48 8B CE", true };
	inline constexpr Site kSkillListCallB{ "SkillListCallB", { 0, 0 }, { 52532, 0x5AC }, "", "48 8B CF E8 ?? ?? ?? ?? 48 8B CF E8 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ??", true };
	inline constexpr Site kSkillListCallC{ "SkillListCallC", { 0, 0 }, { 52591, 0x246 }, "", "48 8B 4E 10 E8 ?? ?? ?? ?? 48 8B 5E 10", true };

	// ActorValueOwner::GetClampedActorValue: the engine's generic clamp of an
	// actor value by its ActorValueInfo flags -- skills to [0, 100]. Spell
	// cost, barter Speech, Lockpicking, Pickpocket and Sneak read the skill
	// through it, so it is replaced whole and lifts the ceiling for the
	// player's skills only (hook-points.md §5.2). The bytes are its entry:
	// push rbx; sub rsp, 0x20; mov rax, [rcx]; movsxd rbx, edx; mov edx, ebx;
	// call [rax+8] (GetActorValue). AE/1.7.x only.
	inline constexpr Site kGetClampedActorValue{
		"GetClampedActorValue",
		{ 0, 0 },
		{ 27284, 0 },
		"",
		"40 53 48 83 EC 20 48 8B 01 48 63 DA 8B D3 FF 50 08",
		true
	};

	// The two calls to the spell-cost skill factor (26505), which takes
	// (ActorValueOwner* caster, float cost, int skill) and reads the skill as
	// an int. Wrapped so the skill is capped where the factor reaches
	// fSpellCostLimit, for any skill (FR-25). AE/1.7.x only.
	inline constexpr std::uint32_t kSpellCostCallOffsetA = 11;
	inline constexpr std::uint32_t kSpellCostCallOffsetB = 12;

	inline constexpr Site kSpellCostCallA{ "SpellCostCallA", { 0, 0 }, { 11017, 0x38 }, "", "F3 44 0F 2C C0 48 8B CB 0F 28 CE E8 ?? ?? ?? ??", true };
	inline constexpr Site kSpellCostCallB{ "SpellCostCallB", { 0, 0 }, { 11494, 0x4C }, "", "48 8D 8B B8 00 00 00 F3 44 0F 2C C0 E8 ?? ?? ?? ??", true };

	// --- Enchanting charge cost (hook-points.md §5.3, FR-30) -----------------

	// Two bytes in the original pattern vary (B0 or B8); stored as "??".
	inline constexpr Site kCalculateChargePointsPerUse{
		"CalculateChargePointsPerUse",
		{ 50557, 0x344 },
		{ 51449, 0x314 },
		"48 8B 0D ?? ?? ?? ?? 48 81 C1 ?? 00 00 00 48 8B 01 BA 17 00 00 00 FF 50 08 0F 28 C8 0F 28 C7 E8 ?? ?? ?? ??",
		"48 8B 0D ?? ?? ?? ?? 48 81 C1 ?? 00 00 00 48 8B 01 BA 17 00 00 00 FF 50 08 0F 28 C8 0F 28 C7 E8 ?? ?? ?? ??",
		true
	};

	// The call to the charge-per-use function (26494), its only caller, at the
	// end of the pattern. Max charge (soul charge through perk entry 0x4F)
	// never reads the skill, so it needs no site (hook-points.md §5.3).
	inline constexpr std::uint32_t kChargeCallOffset = 31;

	// --- Legendary box (hook-points.md §7.1, FR-14) --------------------------
	//
	// Pressing Legendary in the skill tree makes StatsMenu build a
	// LegendarySkillResetConfirmCallback and show it through one call to
	// MessageBoxMenu::Create(message, callback, 0, 25, 10, buttons). Two
	// StatsMenu functions do this -- two input paths into the same box -- and
	// both were found by cross-referencing the callback's vtable (AE id 215997)
	// in SkyrimSE.exe 1.7.104. Replacing that call replaces the box.
	//
	// Each pattern is the argument setup and the call itself:
	//   xor r8d, r8d            ; buttonPressOffset = 0
	//   lea rdx, [rbp+...]      ; the callback smart pointer
	//   lea rcx, [rsp+0x58]     ; the message
	//   call MessageBoxMenu::Create
	//
	// AE/1.7.x only: no SE binary was available to find the SE ids, and the
	// menu is pointless where the cap hook is not installed.

	// Bytes from each pattern's start to its call instruction.
	inline constexpr std::uint32_t kLegendaryCallOffset = 15;

	inline constexpr Site kLegendaryBoxA{
		"LegendaryBoxA",
		{ 0, 0 },
		{ 52510, 0x5E3 },
		"",
		"45 33 C0 48 8D 95 28 01 00 00 48 8D 4C 24 58 E8 ?? ?? ?? ??",
		true
	};

	inline constexpr Site kLegendaryBoxB{
		"LegendaryBoxB",
		{ 0, 0 },
		{ 52520, 0x25C },
		"",
		"45 33 C0 48 8D 95 08 01 00 00 48 8D 4C 24 58 E8 ?? ?? ?? ??",
		true
	};

	// --- Barter (hook-points.md §6, FR-26) ----------------------------------

	// The barter menu's final price clamp, after perk entries 8 and 0x3C:
	// buy = max(fMinBuyMult, buy, 1.0) at [rbp+0xA0], sell = min(fMaxSellMult,
	// sell, 1.0) at [rbp+0x98]. Only xmm1 (= buy) is live after it. Replaced
	// whole by a call to a stub that adds the 1.2 ratio floor. AE/1.7.x only.
	inline constexpr std::size_t kBarterFinalClampLength = 64;

	inline constexpr Site kBarterFinalClamp{
		"BarterFinalClamp",
		{ 0, 0 },
		{ 50945, 0x80A },
		"",
		"F3 0F 10 0D ?? ?? ?? ?? F3 0F 5F 8D A0 00 00 00 F3 0F 5F 0D ?? ?? ?? ?? F3 0F 11 8D A0 00 00 00 F3 0F 10 05 ?? ?? ?? ?? F3 0F 5D 85 98 00 00 00 F3 0F 5D 05 ?? ?? ?? ?? F3 0F 11 85 98 00 00 00",
		true
	};

	inline constexpr std::array kAllSites{
		&kGetSkillCap,
		&kImprovePlayerSkillPoints,
		&kImproveLevelExpBySkillLevel,
		&kPlayerAVOGetCurrent,
		&kUpdateSkillList,
		&kSkillListCallA,
		&kSkillListCallB,
		&kSkillListCallC,
		&kGetClampedActorValue,
		&kSpellCostCallA,
		&kSpellCostCallB,
		&kCalculateChargePointsPerUse,
		&kLegendaryBoxA,
		&kLegendaryBoxB,
		&kBarterFinalClamp,
	};

	// --- Game settings and functions (no patching) ---------------------------

	struct Object
	{
		std::string_view name;
		std::uint64_t    se;
		std::uint64_t    ae;
	};

	inline constexpr Object kEnchantingSkillCostBase{ "fEnchantingSkillCostBase", 506021, 375771 };
	inline constexpr Object kEnchantingSkillCostMult{ "fEnchantingSkillCostMult", 506023, 375774 };
	inline constexpr Object kEnchantingSkillCostScale{ "fEnchantingSkillCostScale", 506025, 375777 };
	inline constexpr Object kEnchantingCostExponent{ "fEnchantingCostExponent", 506027, 375780 };
	inline constexpr Object kXPPerSkillRank{ "fXPPerSkillRank", 505484, 374914 };
	inline constexpr Object kLegendarySkillResetValue{ "fLegendarySkillResetValue", 507065, 377771 };
	inline constexpr Object kPlayerAVOGetBase{ "PlayerAVOGetBase", 37519, 38464 };
	inline constexpr Object kPlayerAVOModBase{ "PlayerAVOModBase", 37521, 38466 };
	inline constexpr Object kPlayerAVOModCurrent{ "PlayerAVOModCurrent", 37522, 38467 };
}
