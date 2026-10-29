// Native unit tests for src/CustomSkillsData.h: the per-skill settings of
// Custom Skills Framework skills (parsing, warnings).
// Build and run on the host (no game needed):
//   c++ -std=c++20 -I src tests/test_customskills.cpp -o build/test_customskills && build/test_customskills

#include "CustomSkillsData.h"

#include <cstdio>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

using namespace BM::CustomSkillsData;

static int g_failures = 0;

#define CHECK(cond)                                                     \
	do {                                                                \
		if (!(cond)) {                                                  \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			++g_failures;                                               \
		}                                                               \
	} while (0)

static bool Contains(const std::vector<std::string>& a_list, const char* a_text)
{
	for (const auto& s : a_list) {
		if (s.find(a_text) != std::string::npos) {
			return true;
		}
	}
	return false;
}

static void TestFormRef()
{
	const auto ref = ParseFormRef("myskilltest.esp|D63");
	CHECK(ref.has_value());
	if (ref) {
		CHECK(ref->plugin == "myskilltest.esp");
		CHECK(ref->localID == 0xD63);
	}
	CHECK(!ParseFormRef("x.esp|"));
	CHECK(!ParseFormRef("|D63"));
	CHECK(!ParseFormRef("x.esp|ZZ"));
	CHECK(!ParseFormRef(""));
	CHECK(!ParseFormRef("x.esp"));
	CHECK(!ParseFormRef("x.esp|100000000"));
}

static void TestParseSettings()
{
	{
		const auto p = ParseSettings({ "true", "own" }, true);
		CHECK(p.warnings.empty());
		CHECK(p.settings.enable);
		CHECK(p.settings.perkSource == PerkSource::kOwn);
	}
	{
		const auto p = ParseSettings({ "", "" }, false);
		CHECK(p.warnings.empty());
		CHECK(!p.settings.enable);
		CHECK(p.settings.perkSource == PerkSource::kPlayer);
	}
	{
		const auto p = ParseSettings({ "", "own" }, false);
		CHECK(p.settings.perkSource == PerkSource::kPlayer);
		CHECK(p.warnings.size() == 1 && Contains(p.warnings, "own"));
	}
	{
		const auto p = ParseSettings({ "", "bogus" }, true);
		CHECK(p.settings.perkSource == PerkSource::kPlayer);
		CHECK(p.warnings.size() == 1);
	}
	for (const char* yes : { "true", "TRUE", "1", "yes", "on", " On " }) {
		CHECK(ParseSettings({ yes, "" }, true).settings.enable);
	}
	for (const char* no : { "false", "0", "no", "off" }) {
		const auto p = ParseSettings({ no, "" }, true);
		CHECK(!p.settings.enable && p.warnings.empty());
	}
	{
		const auto p = ParseSettings({ "maybe", "" }, true);
		CHECK(!p.settings.enable);
		CHECK(p.warnings.size() == 1);
	}
}

static void TestSectionName()
{
	CHECK(SectionName("lolligagging") == "CustomSkill.lolligagging");
}

static void TestUniqueIndex()
{
	const std::vector<std::string> names{ "Lockpicking+", "Twin", "Twin", "Cooking" };
	CHECK(UniqueIndex(names, "Cooking") == std::optional<std::size_t>{ 3 });
	CHECK(UniqueIndex(names, "Lockpicking+") == std::optional<std::size_t>{ 0 });
	CHECK(!UniqueIndex(names, "Twin"));  // shared: cannot pick one (Review Focus 5)
	CHECK(!UniqueIndex(names, "Missing"));
	CHECK(!UniqueIndex({}, ""));
}

static void TestCustomActorValue()
{
	// CSF numbers its skills ('CSF' << 8) + tree index.
	CHECK(kCustomActorValueBase == 0x43534600);
	CHECK(IsCustomActorValue(0x43534600));
	CHECK(IsCustomActorValue(0x43534605));
	CHECK(!IsCustomActorValue(0x435345FF));
	CHECK(!IsCustomActorValue(0));
	CHECK(!IsCustomActorValue(23));   // kOneHanded
	CHECK(!IsCustomActorValue(163));  // past the last vanilla value
	CHECK(!IsCustomActorValue(-1));   // kNone
}

static void TestCapsCodec()
{
	const std::map<std::string, CustomCap> caps{ { "lolligagging", { 110, 1 } }, { "gone_mod_skill", { 300, 3 } } };
	const auto                             bytes = EncodeCaps(caps);
	// u32 count, then per entry u16 length, id, u16 cap, u8 gates
	CHECK(bytes.size() == 4 + (2 + 12 + 3) + (2 + 14 + 3));
	CHECK(std::to_integer<int>(bytes[0]) == 2 && std::to_integer<int>(bytes[1]) == 0);
	const auto back = DecodeCaps(bytes);
	CHECK(back && *back == caps);  // the removed mod's entry survives

	CHECK(DecodeCaps(EncodeCaps({}))->empty());
	for (std::size_t n = 0; n < bytes.size(); ++n) {
		CHECK(!DecodeCaps(std::span(bytes).first(n)));
	}
	auto trailing = bytes;
	trailing.push_back(std::byte{ 0 });
	CHECK(!DecodeCaps(trailing));

	CHECK(!DecodeCaps(EncodeCaps({ { "x", { 105, 0 } } })));
	CHECK(!DecodeCaps(EncodeCaps({ { "x", { 90, 0 } } })));
	CHECK((CustomCap{}.cap == 100 && CustomCap{}.gatesPaid == 0));
}

static void TestCapsRecordLength()
{
	CHECK(IsPlausibleCapsRecordLength(0));
	CHECK(IsPlausibleCapsRecordLength(4));
	CHECK(IsPlausibleCapsRecordLength(kMaxCapsRecordLength));
	CHECK(!IsPlausibleCapsRecordLength(kMaxCapsRecordLength + 1));
	CHECK(!IsPlausibleCapsRecordLength(0xFFFFFFFF));  // a garbage header
}

int main()
{
	TestCapsCodec();
	TestCapsRecordLength();
	TestUniqueIndex();
	TestCustomActorValue();
	TestFormRef();
	TestParseSettings();
	TestSectionName();
	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("test_customskills: all checks passed\n");
	return 0;
}
