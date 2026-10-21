#include "Config.h"
#include "HookSites.h"
#include "Hooks.h"
#include "State.h"

// Milestone 1: plugin loads on SE and AE, reads the INI, persists caps in
// the co-save and detects conflicting plugins. Engine hooks (docs/hook-points.md)
// are added after their sites are verified in a disassembler.

namespace
{
	std::vector<std::string> g_conflicts;

	void DetectConflicts()
	{
		for (const auto& name : BM::Config::Get().knownConflicts) {
			if (SKSE::GetPluginInfo(name)) {
				logger::warn("Conflicting SKSE plugin loaded: {}", name);
				g_conflicts.push_back(name);
			}
		}
	}

	// One notification per session covering both signals (FR-20): a known
	// conflicting plugin, and a patch site whose bytes another plugin already
	// changed. Two separate message boxes would violate success criterion 6.
	void NotifyConflicts()
	{
		const bool mismatch = BM::Hooks::AnySignatureMismatch();
		if ((g_conflicts.empty() && !mismatch) || !BM::Config::Get().warnOnConflict) {
			return;
		}

		// RE::DebugMessageBox resolves relocations of its own (the message box
		// menu and the game setting collection), so calling it without an
		// address library would terminate the process from inside
		// CommonLibSSE -- the very crash the guard in InstallAll avoids. The
		// log already carries the detail in that state.
		if (BM::Hooks::AddressLibraryMissing()) {
			logger::warn("Not showing the conflict warning: no address library, so no engine call "
						 "is safe. See the critical entry above.");
			return;
		}

		std::string msg = "Beyond Mastery: "s;
		if (!g_conflicts.empty()) {
			std::string list;
			for (const auto& n : g_conflicts) {
				list += list.empty() ? n : ", " + n;
			}
			msg += std::format("conflicting plugin(s) loaded: {}. ", list);
		}
		if (mismatch) {
			msg += "some engine patch sites did not match and were skipped. "sv;
		}
		msg += "See BeyondMastery.log."sv;
		RE::DebugMessageBox(msg.c_str());
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostLoad:
			DetectConflicts();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			// Check the sites first so NotifyConflicts can fold a signature
			// mismatch into the same single warning.
			BM::Hooks::InstallAll();
			NotifyConflicts();
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	// trampolineSize covers the cap stub here and the XP stubs to come.
	SKSE::Init(a_skse, { .trampoline = true, .trampolineSize = 256 });

	const auto ver = a_skse->RuntimeVersion();
	logger::info("Beyond Mastery {} loading on Skyrim {}", SKSE::GetPluginVersion().string(), ver.string());

	// PRD §4: SE 1.5.97, AE 1.6.x and 1.7.x. On anything older the hook sites
	// are unknown, so load but stay inert rather than refuse -- returning false
	// here would take the player's whole session down with us.
	const BM::HookSites::RuntimeVersion runtime{ ver.major(), ver.minor(), ver.patch(), ver.build() };
	if (!BM::HookSites::IsSupportedRuntime(runtime)) {
		logger::critical("Skyrim {} is not a supported runtime (oldest supported is {}.{}.{}). "
						 "Beyond Mastery will stay inactive.",
			ver.string(), BM::HookSites::kMinRuntime.major, BM::HookSites::kMinRuntime.minor,
			BM::HookSites::kMinRuntime.patch);
		return true;
	}
	if (!BM::HookSites::IsKnownRuntime(runtime)) {
		logger::warn("Skyrim {} is newer than the newest build whose hook sites we verified "
					 "({}.{}.{}). Addresses come from your address library, but the byte "
					 "signatures may no longer match; check the log below.",
			ver.string(), BM::HookSites::kMaxKnownRuntime.major,
			BM::HookSites::kMaxKnownRuntime.minor, BM::HookSites::kMaxKnownRuntime.patch);
	}

	BM::Config::Get().Load();
	BM::State::RegisterSerialization();

	if (!SKSE::GetMessagingInterface()->RegisterListener(OnMessage)) {
		logger::critical("Could not register messaging listener");
		return false;
	}

	logger::info("Loaded");
	return true;
}
