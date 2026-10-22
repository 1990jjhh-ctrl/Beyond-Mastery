#include "Hooks.h"

#include "Config.h"
#include "Curves.h"
#include "Skills.h"
#include "State.h"
#include "Stubs.h"

#include <filesystem>
#include <format>

namespace BM::Hooks
{
	namespace
	{
		bool g_addressLibraryMissing = false;
		bool g_signatureMismatch = false;
		bool g_capHookInstalled = false;
		bool g_xpGainHookInstalled = false;

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

		// Sites this milestone actually patches. The other seven are resolved
		// and reported because that is useful confidence, but a mismatch there
		// is not this build's problem: warning the player about a function we
		// never touch is a false positive, and the later plans will claim them.
		[[nodiscard]] bool SiteIsNeeded(const HookSites::Site& a_site) noexcept;

		struct Checked
		{
			SiteStatus     status{ SiteStatus::kUnresolved };
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

			const auto  offset = UseAE() ? a_site.ae.offset : a_site.se.offset;
			const auto  pattern = UseAE() ? a_site.patternAE : a_site.patternSE;
			const auto  address = base + offset;
			const auto* at = reinterpret_cast<const std::uint8_t*>(address);
			return { Patterns::Matches(pattern, at) ? SiteStatus::kReady : SiteStatus::kSignatureMismatch,
				address };
		}
	}

	namespace
	{
		// Both installers write a 5-byte call and NOP the rest of the region
		// they replace, so no fragment of a part-overwritten instruction can
		// execute. The region lengths live in Stubs.h beside the stubs.

		bool InstallXPGainHook(std::uintptr_t a_siteAddress)
		{
			Stubs::XPGainStub code{ reinterpret_cast<std::uintptr_t>(&ImprovePlayerSkillPoints) };
			code.ready();

			auto& trampoline = SKSE::GetTrampoline();
			auto* stub = trampoline.allocate(code.getSize());
			if (stub == nullptr) {
				logger::error("No trampoline space for the XP gain stub ({} bytes)", code.getSize());
				return false;
			}
			std::memcpy(stub, code.getCode(), code.getSize());

			trampoline.write_call<5>(a_siteAddress, reinterpret_cast<std::uintptr_t>(stub));
			std::array<std::uint8_t, Stubs::kXPGainLength - 5> nops{};
			nops.fill(REL::NOP);
			REL::safe_write(a_siteAddress + 5, nops.data(), nops.size());

			logger::info("XP gain hook installed at {:#x} (stub {:#x})", a_siteAddress,
				reinterpret_cast<std::uintptr_t>(stub));
			return true;
		}

		bool InstallCapHook(std::uintptr_t a_siteAddress)
		{
			// kCapLoadOffset and the stub's register choices are AE/1.7.x only;
			// SE puts the load at a different offset and the cap in xmm6. The
			// caller checks this too, but a mis-patch here is unrecoverable.
			if (!UseAE()) {
				logger::error("Refusing to install the cap hook on a non-AE runtime");
				return false;
			}

			const auto target = a_siteAddress + Stubs::kCapLoadOffset;

			// Generate first: the allocation must be the size of the emitted
			// code, not sizeof(CapStub), which is the size of the generator
			// object and unrelated to the instructions it produced.
			Stubs::CapStub code{ reinterpret_cast<std::uintptr_t>(&GetSkillCap) };
			code.ready();

			auto& trampoline = SKSE::GetTrampoline();
			auto* stub = trampoline.allocate(code.getSize());
			if (stub == nullptr) {
				logger::error("No trampoline space for the cap stub ({} bytes)", code.getSize());
				return false;
			}
			std::memcpy(stub, code.getCode(), code.getSize());

			trampoline.write_call<5>(target, reinterpret_cast<std::uintptr_t>(stub));
			constexpr std::uint8_t nops[Stubs::kCapLoadLength - 5]{ REL::NOP, REL::NOP, REL::NOP, REL::NOP };
			REL::safe_write(target + 5, nops, sizeof(nops));

			logger::info("Cap hook installed at {:#x} (stub {:#x})", target,
				reinterpret_cast<std::uintptr_t>(stub));
			return true;
		}
	}

	float GetSkillCap(RE::ActorValue a_av) noexcept
	{
		return static_cast<float>(State::Get().LimitOf(a_av));
	}

	float ImprovePlayerSkillPoints(RE::ActorValue a_av, float a_improveMult, float a_points,
		float a_improveOffset) noexcept
	{
		const float vanilla = a_improveMult * a_points + a_improveOffset;
		if (!Skills::IsSkill(a_av)) {
			return vanilla;
		}

		// The base level, deliberately not the current one: Plan 3 remaps the
		// current value for the formula scaling, and the tier here must follow
		// the real level (FR-9).
		const auto* player = RE::PlayerCharacter::GetSingleton();
		if (player == nullptr) {
			return vanilla;
		}
		const auto level = player->AsActorValueOwner()->GetBaseActorValue(a_av);

		const auto factor = Curves::XPGainMultiplier(level, Config::Get().xpGainFactorPerTier);
		return static_cast<float>(vanilla * factor);
	}

	bool XPGainHookInstalled() noexcept
	{
		return g_xpGainHookInstalled;
	}

	bool CapHookInstalled() noexcept
	{
		return g_capHookInstalled;
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

	bool AddressLibraryMissing() noexcept
	{
		return g_addressLibraryMissing;
	}

	namespace
	{
		bool SiteIsNeeded(const HookSites::Site& a_site) noexcept
		{
			if (&a_site == &HookSites::kGetSkillCap) {
				return UseAE();  // SE's patch shape is unverified; see HookSites.h
			}
			if (&a_site == &HookSites::kImprovePlayerSkillPoints) {
				return UseAE() && Config::Get().xpGainFactorPerTier < 1.0;
			}
			return false;
		}
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
			const bool needed = SiteIsNeeded(*site);

			if (checked.status == SiteStatus::kReady) {
				logger::info("Hook site {} resolved to {:#x}: {}{}", site->name, checked.address,
					StatusName(checked.status), needed ? "" : " (not used by this build yet)");
			} else if (needed) {
				logger::error("Hook site {} at {:#x}: {} -- not patching it", site->name,
					checked.address, StatusName(checked.status));
				allReady = false;
				if (checked.status == SiteStatus::kSignatureMismatch) {
					g_signatureMismatch = true;
				}
			} else {
				logger::warn("Hook site {} at {:#x}: {} -- not used by this build yet, ignoring",
					site->name, checked.address, StatusName(checked.status));
			}
		}

		// Cap enforcement (FR-2, FR-8). Only the AE/1.7.x form of this site is
		// established: we disassembled 1.7.104 and know the skill is in esi and
		// the cap belongs in xmm10. SE 1.5.97 arranges it differently (the cap
		// lands in xmm6) and we have no 1.5.97 binary to confirm the register
		// holding the skill, so that site is left alone rather than patched on
		// a guess -- see the note on kGetSkillCap in HookSites.h.
		const auto capStatus = CheckDetailed(HookSites::kGetSkillCap);
		if (!UseAE()) {
			logger::warn(
				"Cap enforcement is not yet available on SE 1.5.97: the patch shape for "
				"this runtime is unverified. Caps are stored and persist, but skills "
				"still stop at 100.");
		} else if (capStatus.status == SiteStatus::kReady) {
			g_capHookInstalled = InstallCapHook(capStatus.address);
		}

		// XP gain factor (FR-9). At the default f = 1.0 the hook would return
		// exactly the vanilla value, so it is not installed at all: fewer
		// hooks, fewer conflicts (hook-points.md §2).
		const auto factor = Config::Get().xpGainFactorPerTier;
		if (factor >= 1.0) {
			logger::info(
				"fXPGainFactorPerTier is {}, so the XP gain hook is a no-op and is not "
				"installed",
				factor);
		} else if (!UseAE()) {
			logger::warn(
				"The XP gain factor is not yet available on SE 1.5.97: that patch shape "
				"is unverified.");
		} else {
			const auto xpSite = CheckDetailed(HookSites::kImprovePlayerSkillPoints);
			if (xpSite.status == SiteStatus::kReady) {
				g_xpGainHookInstalled = InstallXPGainHook(xpSite.address);
			}
		}

		if (Config::Get().clampCharXPAbove100) {
			logger::warn(
				"bClampCharXPAbove100 is set, but the character-XP clamp (FR-11) is not "
				"implemented yet, so the setting currently has no effect.");
		}

		logger::info("Hook sites checked, {}; cap hook {}; XP gain hook {}",
			allReady ? "all ready" : "some unavailable",
			g_capHookInstalled ? "installed" : "not installed",
			g_xpGainHookInstalled ? "installed" : "not installed");
		return allReady;
	}
}
