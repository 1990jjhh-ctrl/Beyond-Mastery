// Native tests for src/Patterns.h, the byte-signature matcher the hook
// installer uses before it writes anything.

#include "HookSites.h"
#include "Patterns.h"

#include <cstdio>
#include <vector>

static int g_failures = 0;

#define CHECK(cond)                                                     \
	do {                                                                \
		if (!(cond)) {                                                  \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			++g_failures;                                               \
		}                                                               \
	} while (0)

#define CHECK_EQ(a, b)                                                                \
	do {                                                                              \
		const auto _a = (a), _b = (b);                                                \
		if (!(_a == _b)) {                                                            \
			std::printf("FAIL %s:%d: %s = %lld, expected %lld\n", __FILE__, __LINE__, \
				#a, static_cast<long long>(_a), static_cast<long long>(_b));          \
			++g_failures;                                                             \
		}                                                                             \
	} while (0)

static void TestMatches()
{
	const std::uint8_t buf[] = { 0xFF, 0x50, 0x18, 0xAB, 0x0F };
	CHECK(BM::Patterns::Matches("FF 50 18 ?? 0F", buf));
	CHECK(BM::Patterns::Matches("FF 50", buf));            // prefix
	CHECK(!BM::Patterns::Matches("FF 51 18 ?? 0F", buf));  // one byte differs
	CHECK(BM::Patterns::Matches("", buf));                 // empty = no check
	CHECK_EQ(BM::Patterns::Length("FF 50 18 ?? 0F"), std::size_t{ 5 });
	CHECK_EQ(BM::Patterns::Length(""), std::size_t{ 0 });

	// A wildcard matches any byte, including one equal to a neighbour.
	const std::uint8_t wild[] = { 0x00, 0xFF, 0x00 };
	CHECK(BM::Patterns::Matches("00 ?? 00", wild));
}

static void TestMatchesEveryVerifiedPatternAgainstItself()
{
	// Each site's own pattern must match bytes synthesised from it, with
	// wildcards filled by an arbitrary value. Guards against a typo in a
	// pattern making a site permanently unpatchable.
	for (const auto* s : BM::HookSites::kAllSites) {
		for (auto p : { s->patternSE, s->patternAE }) {
			if (p.empty()) {
				continue;
			}
			const std::vector<std::uint8_t> synth = BM::Patterns::Synthesise(p, 0x5A);
			CHECK_EQ(synth.size(), BM::Patterns::Length(p));
			CHECK(BM::Patterns::Matches(p, synth.data()));
		}
	}
}

static void TestSynthesiseFillsWildcards()
{
	const auto v = BM::Patterns::Synthesise("FF ?? 0F", 0x5A);
	CHECK_EQ(v.size(), std::size_t{ 3 });
	CHECK_EQ(v[0], std::uint8_t{ 0xFF });
	CHECK_EQ(v[1], std::uint8_t{ 0x5A });
	CHECK_EQ(v[2], std::uint8_t{ 0x0F });
}

int main()
{
	TestMatches();
	TestMatchesEveryVerifiedPatternAgainstItself();
	TestSynthesiseFillsWildcards();
	if (g_failures == 0) {
		std::printf("All pattern tests passed.\n");
	}
	return g_failures == 0 ? 0 : 1;
}
