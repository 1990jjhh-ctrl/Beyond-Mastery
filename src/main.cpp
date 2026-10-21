#include "Config.h"
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

	void NotifyConflicts()
	{
		if (g_conflicts.empty() || !BM::Config::Get().warnOnConflict) {
			return;
		}
		std::string list;
		for (const auto& n : g_conflicts) {
			list += list.empty() ? n : ", " + n;
		}
		const auto msg = std::format("Beyond Mastery: conflicting plugin(s) loaded: {}. See BeyondMastery.log.", list);
		RE::DebugMessageBox(msg.c_str());
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostLoad:
			DetectConflicts();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			NotifyConflicts();
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);

	const auto ver = a_skse->RuntimeVersion();
	logger::info("Beyond Mastery {} loading on Skyrim {}", SKSE::GetPluginVersion().string(), ver.string());

	BM::Config::Get().Load();
	BM::State::RegisterSerialization();

	if (!SKSE::GetMessagingInterface()->RegisterListener(OnMessage)) {
		logger::critical("Could not register messaging listener");
		return false;
	}

	logger::info("Loaded");
	return true;
}
