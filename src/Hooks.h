#pragma once

// Resolution and installation of the engine hooks (docs/hook-points.md).
//
// Every site is resolved through its Address Library ID and its expected
// instruction bytes are re-checked at runtime before anything is written. A
// mismatch means the function changed or another plugin already patched it, so
// the site is skipped and logged rather than patched blind (FR-19, FR-20).

#include "HookSites.h"
#include "Patterns.h"

namespace BM::Hooks
{
	enum class SiteStatus : std::uint8_t
	{
		kReady,              // resolved and the expected bytes are present
		kSignatureMismatch,  // resolved but the bytes differ -- do not patch
		kUnresolved,         // the address could not be resolved at all
		kNotOnThisRuntime    // the site was only located on the other runtime
	};

	[[nodiscard]] std::string_view StatusName(SiteStatus a_status) noexcept;

	// Resolves a site and compares its bytes. Does not write anything.
	[[nodiscard]] SiteStatus Check(const HookSites::Site& a_site);

	// Checks every site and logs one line each. Returns false if any site the
	// current configuration needs is not kReady.
	bool InstallAll();

	// True when any site checked this session reported kSignatureMismatch, so
	// the caller can fold it into the single FR-20 warning.
	[[nodiscard]] bool AnySignatureMismatch() noexcept;

	// True when the Address Library for this runtime is absent. Nothing that
	// resolves a relocation may run in that state -- including
	// RE::DebugMessageBox, which resolves several of its own.
	[[nodiscard]] bool AddressLibraryMissing() noexcept;

	// Callee of the cap trampoline: the limit for a_av (PRD §5) -- the cap, or
	// the first gate not yet paid this climb -- or the vanilla 100 for anything
	// that is not a skill. Replaces the constant 100.0 the XP routine would
	// otherwise load (FR-2, FR-8, FR-13).
	float GetSkillCap(RE::ActorValue a_av) noexcept;

	// True once the cap site has actually been patched.
	[[nodiscard]] bool CapHookInstalled() noexcept;

	// Callee of the XP-gain trampoline: the skill XP gained from use, the
	// vanilla use mult * points + use offset, scaled by sqrt(100 / level) above
	// 100 (FR-9). Installed only with bScaleXPGainAbove100.
	float ImprovePlayerSkillPoints(RE::ActorValue a_av, float a_useMult, float a_points, float a_useOffset) noexcept;

	[[nodiscard]] bool XPGainHookInstalled() noexcept;

	// True once both Legendary box call sites route to Menu::OpenLegendary.
	[[nodiscard]] bool LegendaryMenuInstalled() noexcept;

	// True once Custom Skills Framework's level limit asks
	// CustomSkills::LevelLimit (FR-35), so a raised custom cap is enforced.
	[[nodiscard]] bool CustomSkillLimitInstalled() noexcept;

	// True once formula scaling (the remap, the clamp and the skills-menu
	// calls) is installed (FR-24 ... FR-29).
	[[nodiscard]] bool ScalingInstalled() noexcept;
}
