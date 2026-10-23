#include "Hooks.h"

#include "Config.h"
#include "Curves.h"
#include "Menu.h"
#include "Scaling.h"
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
		bool g_legendaryMenuInstalled = false;
		bool g_scalingInstalled = false;

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

		// Every site formula scaling patches or checks a call against
		// (hook-points.md §5, §6). AE/1.7.x only.
		constexpr std::array kScalingSites{
			&HookSites::kPlayerAVOGetCurrent,
			&HookSites::kUpdateSkillList,
			&HookSites::kSkillListCallA,
			&HookSites::kSkillListCallB,
			&HookSites::kSkillListCallC,
			&HookSites::kGetClampedActorValue,
			&HookSites::kCalculateChargePointsPerUse,
			&HookSites::kBarterFinalClamp,
		};

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
			if (!HookSites::ExistsOn(a_site, UseAE())) {
				return { SiteStatus::kNotOnThisRuntime, 0 };
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
		auto& state = State::Get();

		// Paid gates exist only at level 100 and above, so a skill below 100
		// that still holds some has been reset: it starts a new climb (FR-13).
		// This runs on every XP gain, including each level from 15 back up to
		// 100, so it catches a Legendary reset whenever it lands -- even if
		// vanilla applies it after Menu's own check -- and never fires when
		// Legendary declined, because then the level never dropped.
		if (Skills::IsSkill(a_av)) {
			const auto index = Skills::ToIndex(a_av);
			if (state.GatesPaid(index) > 0) {
				const auto* player = RE::PlayerCharacter::GetSingleton();
				if (player && player->AsActorValueOwner()->GetBaseActorValue(a_av) < 100.0f) {
					state.ClearGates(index);
				}
			}
		}
		return static_cast<float>(state.LimitOf(a_av));
	}

	bool LegendaryMenuInstalled() noexcept
	{
		return g_legendaryMenuInstalled;
	}

	bool ScalingInstalled() noexcept
	{
		return g_scalingInstalled;
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
		case SiteStatus::kNotOnThisRuntime:
			return "kNotOnThisRuntime"sv;
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
			if (&a_site == &HookSites::kLegendaryBoxA || &a_site == &HookSites::kLegendaryBoxB) {
				return UseAE();  // AE/1.7.x only, like the cap hook it depends on
			}
			for (const auto* scaling : kScalingSites) {
				if (&a_site == scaling) {
					return UseAE() && Config::Get().enableFormulaScaling;
				}
			}
			return false;
		}

		// MessageBoxMenu::Create, resolved the way CommonLibSSE resolves it: AE
		// was recompiled at 1.6.1130, which moved its id.
		[[nodiscard]] std::uintptr_t MessageBoxCreateAddress()
		{
			static REL::Relocation<std::uintptr_t> create{ RELOCATION_ID(51421,
				AE_CHECK(SKSE::RUNTIME_SSE_1_6_1130, 52270, 442726)) };
			return create.address();
		}

		// Where a 5-byte `call rel32` at a_call goes.
		[[nodiscard]] std::uintptr_t CallTarget(std::uintptr_t a_call) noexcept
		{
			const auto rel = *reinterpret_cast<const std::int32_t*>(a_call + 1);
			return a_call + 5 + static_cast<std::intptr_t>(rel);
		}

		// Both Legendary sites or neither: one hooked and one not would show our
		// menu on one input path and vanilla's box on the other. Each call must
		// still go to MessageBoxMenu::Create -- the byte pattern wildcards the
		// call's target, so this is what catches another mod that already
		// redirected it.
		//
		// A mod that changes Legendary itself needs a compatibility patch; this
		// plugin does not try to chain onto it.
		[[nodiscard]] bool InstallLegendaryMenu(std::uintptr_t a_siteA, std::uintptr_t a_siteB)
		{
			const auto                                                             create = MessageBoxCreateAddress();
			const std::array<std::pair<const HookSites::Site*, std::uintptr_t>, 2> sites{ {
				{ &HookSites::kLegendaryBoxA, a_siteA + HookSites::kLegendaryCallOffset },
				{ &HookSites::kLegendaryBoxB, a_siteB + HookSites::kLegendaryCallOffset },
			} };
			for (const auto& [site, call] : sites) {
				if (CallTarget(call) != create) {
					logger::error(
						"{}: the call at {:#x} no longer goes to MessageBoxMenu::Create -- another "
						"mod has redirected it and needs a compatibility patch. Not installing the "
						"Legendary menu.",
						site->name, call);
					g_signatureMismatch = true;
					return false;
				}
			}
			for (const auto& [site, call] : sites) {
				SKSE::GetTrampoline().write_call<5>(call, &Menu::OpenLegendary);
				logger::info("{} hooked at {:#x}", site->name, call);
			}
			return true;
		}

		// Formula scaling (FR-24 ... FR-29): every site is checked before any
		// is written, so scaling is either fully in or fully out. Player only:
		// the remap sits in the player's own vtable, and the clamp lifts the
		// ceiling for the player's skills alone.
		[[nodiscard]] bool InstallScaling()
		{
			// The player's ActorValueOwner vtable, slot 1 (GetActorValue), must
			// still hold the engine's shared function; anything else means
			// another mod got there first.
			REL::Relocation<std::uintptr_t>        vtable{ RE::VTABLE_PlayerCharacter[5] };
			static REL::Relocation<std::uintptr_t> getActorValue{ RELOCATION_ID(37517, 38462) };
			const auto                             slot = *reinterpret_cast<const std::uintptr_t*>(vtable.address() + sizeof(void*));
			if (slot != getActorValue.address()) {
				logger::error(
					"The player's GetActorValue slot no longer holds the engine's function -- another mod "
					"replaced it and needs a compatibility patch. Not installing formula scaling.");
				g_signatureMismatch = true;
				return false;
			}

			const auto clamp = CheckDetailed(HookSites::kGetClampedActorValue);
			const auto list = CheckDetailed(HookSites::kUpdateSkillList);
			if (clamp.status != SiteStatus::kReady || list.status != SiteStatus::kReady) {
				return false;  // the site loop above already logged why
			}

			const std::array<std::pair<const HookSites::Site*, std::uint32_t>, 3> listCalls{ {
				{ &HookSites::kSkillListCallA, HookSites::kSkillListCallOffsetA },
				{ &HookSites::kSkillListCallB, HookSites::kSkillListCallOffsetB },
				{ &HookSites::kSkillListCallC, HookSites::kSkillListCallOffsetC },
			} };
			std::array<std::uintptr_t, 3>                                         calls{};
			for (std::size_t i = 0; i < listCalls.size(); ++i) {
				const auto [site, offset] = listCalls[i];
				const auto checked = CheckDetailed(*site);
				if (checked.status != SiteStatus::kReady) {
					return false;
				}
				calls[i] = checked.address + offset;
				if (CallTarget(calls[i]) != list.address) {
					logger::error(
						"{}: the call at {:#x} no longer goes to UpdateSkillList -- another mod has "
						"redirected it and needs a compatibility patch. Not installing formula scaling.",
						site->name, calls[i]);
					g_signatureMismatch = true;
					return false;
				}
			}

			// Enchanting charge per use (FR-30): its only call, still to 26494.
			const auto charge = CheckDetailed(HookSites::kCalculateChargePointsPerUse);
			if (charge.status != SiteStatus::kReady) {
				return false;
			}
			const auto                             chargeCall = charge.address + HookSites::kChargeCallOffset;
			static REL::Relocation<std::uintptr_t> chargePerUse{ REL::ID(26494) };  // AE id; AE only
			if (CallTarget(chargeCall) != chargePerUse.address()) {
				logger::error(
					"The charge-per-use call at {:#x} no longer goes to the engine's function -- another "
					"mod has redirected it and needs a compatibility patch. Not installing formula scaling.",
					chargeCall);
				g_signatureMismatch = true;
				return false;
			}

			// The barter menu's final price clamp (FR-26).
			const auto barter = CheckDetailed(HookSites::kBarterFinalClamp);
			if (barter.status != SiteStatus::kReady) {
				return false;
			}

			auto&                  trampoline = SKSE::GetTrampoline();
			Stubs::BarterClampStub barterCode{ reinterpret_cast<std::uintptr_t>(&Scaling::BarterClamp) };
			barterCode.ready();
			auto* barterStub = trampoline.allocate(barterCode.getSize());
			if (barterStub == nullptr) {
				logger::error("No trampoline space for the barter stub ({} bytes)", barterCode.getSize());
				return false;
			}
			std::memcpy(barterStub, barterCode.getCode(), barterCode.getSize());

			for (const auto call : calls) {
				trampoline.write_call<5>(call, &Scaling::UpdateSkillList);
			}
			Scaling::SetOriginalChargePerUse(chargePerUse.address());
			trampoline.write_call<5>(chargeCall, &Scaling::ChargePerUse);
			trampoline.write_branch<5>(clamp.address, &Scaling::GetClampedActorValue);
			trampoline.write_call<5>(barter.address, reinterpret_cast<std::uintptr_t>(barterStub));
			std::array<std::uint8_t, HookSites::kBarterFinalClampLength - 5> barterNops{};
			barterNops.fill(REL::NOP);
			REL::safe_write(barter.address + 5, barterNops.data(), barterNops.size());
			// The original first: the slot is live the moment it is written.
			Scaling::SetOriginalGetActorValue(slot);
			vtable.write_vfunc(1, &Scaling::GetActorValue);

			logger::info(
				"Formula scaling installed: GetActorValue slot at {:#x}, clamp at {:#x}, skills menu "
				"calls at {:#x} {:#x} {:#x}",
				vtable.address() + sizeof(void*), clamp.address, calls[0], calls[1], calls[2]);
			Scaling::LogSelfCheck();
			return true;
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
			} else if (checked.status == SiteStatus::kNotOnThisRuntime) {
				// Expected, not a fault: the feature behind it is AE/1.7.x only
				// and says so where it is installed.
				logger::info("Hook site {}: not located on this runtime, skipped", site->name);
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

		// The Legendary menu (FR-14). Only where the cap hook is in: Upgrade
		// raises a cap, and an unenforced cap would be a raise that does nothing.
		if (g_capHookInstalled) {
			const auto a = CheckDetailed(HookSites::kLegendaryBoxA);
			const auto b = CheckDetailed(HookSites::kLegendaryBoxB);
			if (a.status == SiteStatus::kReady && b.status == SiteStatus::kReady) {
				g_legendaryMenuInstalled = InstallLegendaryMenu(a.address, b.address);
				allReady = allReady && g_legendaryMenuInstalled;
			}
		} else {
			// Info, not a warning: why the cap hook is missing was logged above.
			logger::info("Cap hook not installed, so the Legendary menu is not either; Legendary stays vanilla");
		}

		// Formula scaling (FR-24 ... FR-29). Independent of the cap hook: a
		// Fortify effect can lift a skill past 100 without any raised cap.
		if (!Config::Get().enableFormulaScaling) {
			logger::info("bEnableFormulaScaling is off, so formula scaling is not installed");
		} else if (UseAE()) {
			Scaling::CacheSettings();
			g_scalingInstalled = InstallScaling();
			allReady = allReady && g_scalingInstalled;
		}

		logger::info("Hook sites checked, {}; cap hook {}; XP gain hook {}; Legendary menu {}; formula scaling {}",
			allReady ? "all ready" : "some unavailable",
			g_capHookInstalled ? "installed" : "not installed",
			g_xpGainHookInstalled ? "installed" : "not installed",
			g_legendaryMenuInstalled ? "installed" : "not installed",
			g_scalingInstalled ? "installed" : "not installed");
		return allReady;
	}
}
