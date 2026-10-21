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
		kUnresolved          // the address could not be resolved at all
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
}
