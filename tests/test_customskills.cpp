// Native unit tests for src/CustomSkillsData.h: the per-skill settings of
// Custom Skills Framework skills (parsing, warnings).
// Build and run on the host (no game needed):
//   c++ -std=c++20 -I src tests/test_customskills.cpp -o build/test_customskills && build/test_customskills

#include "CustomSkillsData.h"

#include <cstdio>

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

static void TestUniqueNames()
{
	const auto unique = UniqueNames({ "A", "B", "A" });
	CHECK(unique == std::set<std::string>{ "B" });
	CHECK(UniqueNames({}).empty());
	CHECK(UniqueNames({ "X", "Y" }).size() == 2);
}

int main()
{
	TestUniqueNames();
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
