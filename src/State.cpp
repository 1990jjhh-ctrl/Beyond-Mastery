#include "State.h"

namespace BM
{
	namespace
	{
		constexpr std::uint32_t kUniqueID = 'BMST';
		constexpr std::uint32_t kCapsRecord = 'CAPS';
		constexpr std::uint32_t kCapsVersion = 1;
	}

	State& State::Get() noexcept
	{
		static State instance;
		return instance;
	}

	void State::RegisterSerialization()
	{
		const auto* intfc = SKSE::GetSerializationInterface();
		intfc->SetUniqueID(kUniqueID);
		intfc->SetSaveCallback(OnSave);
		intfc->SetLoadCallback(OnLoad);
		intfc->SetRevertCallback(OnRevert);
	}

	void State::OnSave(SKSE::SerializationInterface* a_intfc)
	{
		const auto& caps = Get()._caps;
		if (!a_intfc->WriteRecord(kCapsRecord, kCapsVersion, caps)) {
			logger::error("Failed to write CAPS record");
		}
	}

	void State::OnLoad(SKSE::SerializationInterface* a_intfc)
	{
		auto& state = Get();
		state.Reset();

		std::uint32_t type = 0;
		std::uint32_t version = 0;
		std::uint32_t length = 0;
		while (a_intfc->GetNextRecordInfo(type, version, length)) {
			if (type != kCapsRecord) {
				logger::warn("Unknown co-save record {:08X}, skipped", type);
				continue;
			}
			if (version != kCapsVersion || length != sizeof(state._caps)) {
				logger::error("CAPS record version {} / length {} unsupported, caps reset to 100", version, length);
				continue;
			}
			decltype(state._caps) caps{};
			if (a_intfc->ReadRecordData(caps) != sizeof(caps)) {
				logger::error("CAPS record truncated, caps reset to 100");
				continue;
			}
			for (std::size_t i = 0; i < caps.size(); ++i) {
				// Sanity: caps are multiples of 10 and >= 100.
				const auto c = caps[i];
				state._caps[i] = (c >= kDefaultCap && c % 10 == 0) ? c : kDefaultCap;
			}
		}

		for (std::size_t i = 0; i < state._caps.size(); ++i) {
			if (state._caps[i] != kDefaultCap) {
				logger::info("Loaded cap: skill {} = {}", i, state._caps[i]);
			}
		}
	}

	void State::OnRevert(SKSE::SerializationInterface*)
	{
		Get().Reset();
	}
}
