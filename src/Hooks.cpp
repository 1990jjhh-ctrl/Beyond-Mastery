#include "Hooks.h"

#include <filesystem>
#include <format>

namespace BM::Hooks
{
	namespace
	{
		bool g_addressLibraryMissing = false;
		bool g_signatureMismatch = false;

		// REL::Module classifies a runtime by minor version: 4 = VR, 5 = SE,
		// 6 and above = AE. So 1.7.x takes the AE path throughout -- the AE
		// column of the site table and the versionlib-*.bin file name.
		[[nodiscard]] bool UseAE() noexcept { return REL::Module::IsAE(); }

		[[nodiscard]] std::string AddressLibraryPath()
		{
			const auto version = REL::Module::get().version();
			return UseAE() ?
					   std::format("Data/SKSE/Plugins/versionlib-{}.bin", version.string()) :
					   std::format("Data/SKSE/Plugins/version-{}.bin", version.string());
		}

		struct Checked
		{
			SiteStatus    status{ SiteStatus::kUnresolved };
			std::uintptr_t address{ 0 };
		};

		[[nodiscard]] Checked CheckDetailed(const HookSites::Site& a_site)
		{
			// Resolving through the address library when the file is absent
			// would terminate the game from inside CommonLibSSE, so never get
			// that far (see InstallAll).
			if (g_addressLibraryMissing) {
				return {};
			}

			const REL::RelocationID id{ a_site.se.id, a_site.ae.id };
			const auto              base = id.address();
			if (base == 0) {
				return {};
			}

			const auto offset = UseAE() ? a_site.ae.offset : a_site.se.offset;
			const auto pattern = UseAE() ? a_site.patternAE : a_site.patternSE;
			const auto address = base + offset;
			const auto* at = reinterpret_cast<const std::uint8_t*>(address);
			return { Patterns::Matches(pattern, at) ? SiteStatus::kReady : SiteStatus::kSignatureMismatch,
				address };
		}
	}

	std::string_view StatusName(SiteStatus a_status) noexcept
	{
		switch (a_status) {
		case SiteStatus::kReady:
			return "kReady"sv;
		case SiteStatus::kSignatureMismatch:
			return "kSignatureMismatch"sv;
		default:
			return "kUnresolved"sv;
		}
	}

	SiteStatus Check(const HookSites::Site& a_site)
	{
		return CheckDetailed(a_site).status;
	}

	bool AnySignatureMismatch() noexcept
	{
		return g_signatureMismatch;
	}

	bool InstallAll()
	{
		g_signatureMismatch = false;

		// A missing or wrong-version address library is the single most common
		// way for this plugin to meet a player's game. CommonLibSSE reports it
		// with a message box and then terminates the process, which costs the
		// player their session, so check the file is there before resolving the
		// first ID. The file name carries the exact game version, so a library
		// for another build is simply absent under the name we need.
		const auto libraryPath = AddressLibraryPath();
		g_addressLibraryMissing = !std::filesystem::exists(libraryPath);
		if (g_addressLibraryMissing) {
			logger::critical(
				"Address Library not found: {} is missing. Install \"Address Library for SKSE "
				"Plugins\" for Skyrim {}. Beyond Mastery stays inactive; gameplay is unchanged.",
				libraryPath, REL::Module::get().version().string());
			return false;
		}

		bool allReady = true;
		for (const auto* site : HookSites::kAllSites) {
			const auto checked = CheckDetailed(*site);
			if (checked.status == SiteStatus::kReady) {
				logger::info("Hook site {} resolved to {:#x}: {}", site->name, checked.address,
					StatusName(checked.status));
			} else {
				logger::error("Hook site {} at {:#x}: {} -- not patching it", site->name,
					checked.address, StatusName(checked.status));
			}

			if (checked.status != SiteStatus::kReady) {
				allReady = false;
			}
			if (checked.status == SiteStatus::kSignatureMismatch) {
				g_signatureMismatch = true;
			}
		}

		// Nothing is patched yet: the trampolines arrive with cap enforcement.
		logger::info("Hook sites checked, {}", allReady ? "all ready" : "some unavailable");
		return allReady;
	}
}
