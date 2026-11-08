#include "State.h"

#include "Curves.h"

namespace BM
{
namespace
{
constexpr std::uint32_t kUniqueID = 'BMST';

constexpr std::uint32_t kCapsRecord = 'CAPS';
// A record whose layout changes gets a new version, and OnLoad keeps
// reading the old one: a save must never lose its caps (FR-22).
constexpr std::uint32_t kCapsVersion = 1;

// Separate from CAPS rather than a CAPS version 2, so saves written
// before gates recurred still load their caps untouched.
constexpr std::uint32_t kGatesRecord = 'GATE';
constexpr std::uint32_t kGatesVersion = 1;

// Custom Skills Framework skills, by id (see CustomSkillsData::EncodeCaps).
constexpr std::uint32_t kCustomCapsRecord = 'CCAP';
constexpr std::uint32_t kCustomCapsVersion = 1;

using Caps = std::array<std::uint16_t, Skills::kCount>;
using Gates = std::array<std::uint8_t, Skills::kCount>;

// Reads the CAPS record into a_caps. On a record it cannot use it logs and
// leaves a_caps as it was, so the caps stay at 100.
void LoadCapsRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length, Caps& a_caps)
{
	if (a_version != kCapsVersion || a_length != sizeof(a_caps)) {
		logger::error("CAPS record version {} / length {} unsupported, caps reset to 100", a_version, a_length);
		return;
	}
	Caps caps{};
	if (a_intfc->ReadRecordData(caps) != sizeof(caps)) {
		logger::error("CAPS record truncated, caps reset to 100");
		return;
	}
	for (std::size_t i = 0; i < caps.size(); ++i) {
		// Caps are multiples of 10, at least 100 and at most
		// kMaxPlausibleCap; anything else is damaged data, not a cap.
		const auto c = caps[i];
		a_caps[i] = Curves::IsPlausibleCap(c) ? c : State::kDefaultCap;
	}
}

// Reads the GATE record into a_gates; whether it could.
[[nodiscard]] bool LoadGatesRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length, Gates& a_gates)
{
	if (a_version != kGatesVersion || a_length != sizeof(a_gates)) {
		logger::error("GATE record version {} / length {} unsupported, gates derived from caps", a_version, a_length);
		return false;
	}
	if (a_intfc->ReadRecordData(a_gates) != sizeof(a_gates)) {
		logger::error("GATE record truncated, gates derived from caps");
		return false;
	}
	return true;
}

// Reads the CCAP record into a_customCaps, under a_mutex. On a record it
// cannot use it logs and leaves a_customCaps as it was.
void LoadCustomCapsRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length,
	std::mutex& a_mutex, std::map<std::string, CustomSkillsData::CustomCap>& a_customCaps)
{
	if (a_version != kCustomCapsVersion) {
		logger::error("CCAP record version {} unsupported, custom skill caps reset to 100", a_version);
		return;
	}
	if (!CustomSkillsData::IsPlausibleCapsRecordLength(a_length)) {
		logger::error("CCAP record damaged, custom skill caps reset to 100");
		return;
	}
	std::vector<std::byte> bytes(a_length);
	if (a_intfc->ReadRecordData(bytes.data(), a_length) != a_length) {
		logger::error("CCAP record truncated, custom skill caps reset to 100");
		return;
	}
	auto caps = CustomSkillsData::DecodeCaps(bytes);
	if (!caps) {
		logger::error("CCAP record damaged, custom skill caps reset to 100");
		return;
	}
	const std::lock_guard lock(a_mutex);
	a_customCaps = std::move(*caps);
}
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
	const auto& state = Get();
	if (!a_intfc->WriteRecord(kCapsRecord, kCapsVersion, state._caps)) {
		logger::error("Failed to write CAPS record");
	}
	if (!a_intfc->WriteRecord(kGatesRecord, kGatesVersion, state._gatesPaid)) {
		logger::error("Failed to write GATE record");
	}
	std::vector<std::byte> bytes;
	{
		const std::lock_guard lock(state._customMutex);
		if (!state._customCaps.empty()) {
			bytes = CustomSkillsData::EncodeCaps(state._customCaps);
		}
	}
	if (!bytes.empty()) {
		if (!a_intfc->WriteRecord(kCustomCapsRecord, kCustomCapsVersion, bytes.data(), static_cast<std::uint32_t>(bytes.size()))) {
			logger::error("Failed to write CCAP record");
		}
	}
}

void State::OnLoad(SKSE::SerializationInterface* a_intfc)
{
	auto& state = Get();
	state.Reset();

	// Gates are reconciled with the caps only after every record is read,
	// so the order the records arrive in never matters.
	decltype(state._gatesPaid) loadedGates{};
	bool                       gatesLoaded = false;

	std::uint32_t type = 0;
	std::uint32_t version = 0;
	std::uint32_t length = 0;
	while (a_intfc->GetNextRecordInfo(type, version, length)) {
		if (type == kCapsRecord) {
			LoadCapsRecord(a_intfc, version, length, state._caps);
		} else if (type == kGatesRecord) {
			gatesLoaded = LoadGatesRecord(a_intfc, version, length, loadedGates) || gatesLoaded;
		} else if (type == kCustomCapsRecord) {
			LoadCustomCapsRecord(a_intfc, version, length, state._customMutex, state._customCaps);
		} else {
			logger::warn("Unknown co-save record {:08X}, skipped", type);
		}
	}

	for (std::size_t i = 0; i < state._caps.size(); ++i) {
		const auto needed = Curves::GatesNeededFor(state._caps[i]);
		// Without a GATE record (a save from before gates recurred), every
		// gate below the cap counts as paid, so the cap stays reachable. With
		// one, more gates than the cap has passed can never be needed, which
		// also makes a damaged value harmless.
		state._gatesPaid[i] = gatesLoaded ? std::min(loadedGates[i], needed) : needed;

		if (state._caps[i] != kDefaultCap || state._gatesPaid[i] != 0) {
			logger::info("Loaded skill {}: cap {}, gates paid {}, limit {}", i, state._caps[i],
				state._gatesPaid[i], Curves::Limit(state._caps[i], state._gatesPaid[i]));
		}
	}
	if (!gatesLoaded) {
		logger::info("No GATE record: gates derived from caps (save predates recurring gates)");
	}
}

void State::OnRevert(SKSE::SerializationInterface*)
{
	Get().Reset();
}
}
