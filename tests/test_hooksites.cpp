// Native check that every pattern in src/HookSites.h is well-formed.

#include "HookSites.h"

#include <cctype>
#include <cstdio>

static bool PatternOk(std::string_view a_p)
{
	if (a_p.empty())
		return true;
	std::size_t i = 0;
	while (i < a_p.size()) {
		if (i + 2 > a_p.size())
			return false;
		const char a = a_p[i], b = a_p[i + 1];
		const bool wild = a == '?' && b == '?';
		const bool hex = std::isxdigit(static_cast<unsigned char>(a)) && std::isxdigit(static_cast<unsigned char>(b));
		if (!wild && !hex)
			return false;
		i += 2;
		if (i < a_p.size()) {
			if (a_p[i] != ' ')
				return false;
			++i;
		}
	}
	return true;
}

static int g_failures = 0;

#define CHECK(cond)                                                     \
	do {                                                                \
		if (!(cond)) {                                                  \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			++g_failures;                                               \
		}                                                               \
	} while (0)

static void TestPatternsWellFormed()
{
	for (const auto* site : BM::HookSites::kAllSites) {
		for (auto p : { site->patternSE, site->patternAE }) {
			if (!PatternOk(p)) {
				std::printf("FAIL %.*s: bad pattern \"%.*s\"\n",
					static_cast<int>(site->name.size()), site->name.data(),
					static_cast<int>(p.size()), p.data());
				++g_failures;
			}
		}
	}
}

static void TestRuntimeGate()
{
	using namespace BM::HookSites;
	CHECK(IsSupportedRuntime({ 1, 5, 97, 0 }));
	CHECK(IsSupportedRuntime({ 1, 6, 1170, 0 }));
	CHECK(IsSupportedRuntime({ 1, 7, 104, 0 }));  // the installed runtime
	CHECK(!IsSupportedRuntime({ 1, 4, 15, 0 }));  // pre-SE-1.5.97
	CHECK(!IsSupportedRuntime({ 1, 4, 15, 0 }));  // VR is 1.4.15 (PRD §3)
	CHECK(!IsSupportedRuntime({ 1, 2, 15, 0 }));  // early LE builds
	CHECK(!IsSupportedRuntime({ 1, 5, 96, 0 }));  // older SE builds are not targeted
	// The gate is a lower bound only, so LE 1.9.32 passes it. Harmless: a
	// CommonLibSSE-NG plugin cannot load into LE at all, and SKSE64 would not
	// be there to load it. Asserted so the gap is recorded, not assumed absent.
	CHECK(IsSupportedRuntime({ 1, 9, 32, 0 }));

	// kMaxKnownRuntime is the newest build whose sites we verified ourselves;
	// anything past it still runs, but the plugin says so in the log.
	CHECK(IsKnownRuntime({ 1, 7, 104, 0 }));
	CHECK(IsKnownRuntime({ 1, 5, 97, 0 }));
	CHECK(!IsKnownRuntime({ 1, 8, 0, 0 }));
}

static void TestEverySiteVerified()
{
	// Task 1's scripts/verify_sites.py matched all nine against SkyrimSE.exe
	// 1.7.104 using the 1.7.99 address library.
	for (const auto* s : BM::HookSites::kAllSites)
		CHECK(s->verified);
}

static void TestLegendaryBoxSites()
{
	using namespace BM::HookSites;
	CHECK(kAllSites.size() == 11);
	CHECK(kLegendaryBoxA.ae.id == 52510 && kLegendaryBoxA.ae.offset == 0x5E3);
	CHECK(kLegendaryBoxB.ae.id == 52520 && kLegendaryBoxB.ae.offset == 0x25C);
	CHECK(kLegendaryCallOffset == 15);
	// The call opcode sits exactly kLegendaryCallOffset bytes into each pattern.
	for (const auto* s : { &kLegendaryBoxA, &kLegendaryBoxB })
		CHECK(s->patternAE.substr(kLegendaryCallOffset * 3, 2) == "E8");
}

static void TestExistsOn()
{
	using namespace BM::HookSites;
	// The Legendary sites have no SE ids: absent there, not unresolved.
	for (const auto* s : { &kLegendaryBoxA, &kLegendaryBoxB }) {
		CHECK(!ExistsOn(*s, false));
		CHECK(ExistsOn(*s, true));
	}
	// Every other site exists on both runtimes.
	for (const auto* s : kAllSites) {
		if (s != &kLegendaryBoxA && s != &kLegendaryBoxB)
			CHECK(ExistsOn(*s, false) && ExistsOn(*s, true));
	}
}

int main()
{
	TestPatternsWellFormed();
	TestLegendaryBoxSites();
	TestExistsOn();
	TestRuntimeGate();
	TestEverySiteVerified();
	if (g_failures == 0)
		std::printf("All hook-site checks passed (%zu sites).\n", BM::HookSites::kAllSites.size());
	return g_failures == 0 ? 0 : 1;
}
