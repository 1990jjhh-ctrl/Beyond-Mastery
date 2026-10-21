#include "Hooks.h"

#include "Config.h"
#include "Curves.h"
#include "State.h"

#include <xbyak/xbyak.h>

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

		// Bytes from the cap site's start to the `movss xmm10, [rip+..]` that
		// loads the vanilla 100.0: call [rax+0x18] (3) + movaps xmm8,xmm0 (4).
		constexpr std::uint32_t kCapLoadOffset = 7;

		// Length of that movss, and so of the region we replace.
		constexpr std::size_t kCapLoadLength = 9;

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

	namespace
	{
		// Replaces the vanilla `movss xmm10, [rip+100.0]` with a call to
		// GetSkillCap, so xmm10 carries this skill's own cap into the comiss
		// that follows.
		//
		// At the patch point: esi holds the skill ActorValue (callee-saved, so
		// it survives our call), xmm0 holds the level the engine just fetched
		// and the comiss needs it, and xmm10 is what we must set. xmm0-xmm5 are
		// volatile across a call, so xmm0 is saved here; xmm6-xmm15 are
		// callee-saved, so xmm8 and xmm10 need no saving.
		struct CapStub : Xbyak::CodeGenerator
		{
			explicit CapStub(std::uintptr_t a_callee)
			{
				// After the call into this stub rsp % 16 == 8, so subtracting
				// 0x38 re-aligns it to 16 and leaves 0x20 of shadow space for
				// the callee plus an aligned 16-byte slot at rsp+0x20.
				sub(rsp, 0x38);
				movaps(ptr[rsp + 0x20], xmm0);
				mov(ecx, esi);  // the skill actor value is the only argument
				mov(rax, a_callee);
				call(rax);
				movaps(xmm10, xmm0);  // the cap we just computed
				movaps(xmm0, ptr[rsp + 0x20]);
				add(rsp, 0x38);
				ret();
			}
		};

		// Replaces the three instructions that compute the XP to add:
		//
		//   movss xmm0, [rsp+0x30]   ; ImproveMult, filled by the call above
		//   mulss xmm0, xmm6         ; the points argument, saved in the prologue
		//   addss xmm0, [rsp+0x34]   ; ImproveOffset
		//
		// 16 bytes in total, leaving the result in xmm0 exactly as the engine
		// expects. esi still holds the skill actor value. Our own sub rsp
		// shifts the two stack operands, hence the +0x38.
		struct XPGainStub : Xbyak::CodeGenerator
		{
			explicit XPGainStub(std::uintptr_t a_callee)
			{
				sub(rsp, 0x38);
				mov(ecx, esi);                              // arg 0: actor value
				movss(xmm1, ptr[rsp + 0x38 + 0x30]);        // arg 1: ImproveMult
				movaps(xmm2, xmm6);                         // arg 2: points
				movss(xmm3, ptr[rsp + 0x38 + 0x34]);        // arg 3: ImproveOffset
				mov(rax, a_callee);
				call(rax);                                  // result in xmm0
				add(rsp, 0x38);
				ret();
			}
		};

		// Writes the 5-byte call and NOPs the 4 bytes of the 9-byte movss that
		// it does not cover, so no fragment of the old instruction executes.
		// Length of the three replaced instructions at the XP gain site.
		constexpr std::size_t kXPGainLength = 16;

		bool InstallXPGainHook(std::uintptr_t a_siteAddress)
		{
			XPGainStub code{ reinterpret_cast<std::uintptr_t>(&ImprovePlayerSkillPoints) };
			code.ready();

			auto& trampoline = SKSE::GetTrampoline();
			auto* stub = trampoline.allocate(code.getSize());
			if (stub == nullptr) {
				logger::error("No trampoline space for the XP gain stub ({} bytes)", code.getSize());
				return false;
			}
			std::memcpy(stub, code.getCode(), code.getSize());

			trampoline.write_call<5>(a_siteAddress, reinterpret_cast<std::uintptr_t>(stub));
			std::array<std::uint8_t, kXPGainLength - 5> nops{};
			nops.fill(REL::NOP);
			REL::safe_write(a_siteAddress + 5, nops.data(), nops.size());

			logger::info("XP gain hook installed at {:#x} (stub {:#x})", a_siteAddress,
				reinterpret_cast<std::uintptr_t>(stub));
			return true;
		}

		bool InstallCapHook(std::uintptr_t a_siteAddress)
		{
			const auto target = a_siteAddress + kCapLoadOffset;

			// Generate first: the allocation must be the size of the emitted
			// code, not sizeof(CapStub), which is the size of the generator
			// object and unrelated to the instructions it produced.
			CapStub code{ reinterpret_cast<std::uintptr_t>(&GetSkillCap) };
			code.ready();

			auto& trampoline = SKSE::GetTrampoline();
			auto* stub = trampoline.allocate(code.getSize());
			if (stub == nullptr) {
				logger::error("No trampoline space for the cap stub ({} bytes)", code.getSize());
				return false;
			}
			std::memcpy(stub, code.getCode(), code.getSize());

			trampoline.write_call<5>(target, reinterpret_cast<std::uintptr_t>(stub));
			constexpr std::uint8_t nops[kCapLoadLength - 5]{ REL::NOP, REL::NOP, REL::NOP, REL::NOP };
			REL::safe_write(target + 5, nops, sizeof(nops));

			logger::info("Cap hook installed at {:#x} (stub {:#x})", target,
				reinterpret_cast<std::uintptr_t>(stub));
			return true;
		}
	}

	float GetSkillCap(RE::ActorValue a_av) noexcept
	{
		return static_cast<float>(State::Get().CapOf(a_av));
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

		// Cap enforcement (FR-2, FR-8). Only the AE/1.7.x form of this site is
		// established: we disassembled 1.7.104 and know the skill is in esi and
		// the cap belongs in xmm10. SE 1.5.97 arranges it differently (the cap
		// lands in xmm6) and we have no 1.5.97 binary to confirm the register
		// holding the skill, so that site is left alone rather than patched on
		// a guess -- see the note on kGetSkillCap in HookSites.h.
		const auto capStatus = CheckDetailed(HookSites::kGetSkillCap);
		if (!UseAE()) {
			logger::warn("Cap enforcement is not yet available on SE 1.5.97: the patch shape for "
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
			logger::info("fXPGainFactorPerTier is {}, so the XP gain hook is a no-op and is not "
						 "installed",
				factor);
		} else if (!UseAE()) {
			logger::warn("The XP gain factor is not yet available on SE 1.5.97: that patch shape "
						 "is unverified.");
		} else {
			const auto xpSite = CheckDetailed(HookSites::kImprovePlayerSkillPoints);
			if (xpSite.status == SiteStatus::kReady) {
				g_xpGainHookInstalled = InstallXPGainHook(xpSite.address);
			}
		}

		logger::info("Hook sites checked, {}; cap hook {}; XP gain hook {}",
			allReady ? "all ready" : "some unavailable",
			g_capHookInstalled ? "installed" : "not installed",
			g_xpGainHookInstalled ? "installed" : "not installed");
		return allReady;
	}
}
