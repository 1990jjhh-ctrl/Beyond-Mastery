// Native tests for src/Texts.h: the player-facing text that depends on state.

#include "Texts.h"

#include <cstdio>
#include <string>

static int g_failures = 0;

#define CHECK_EQ(actual, expected)                                                          \
	do {                                                                                    \
		const std::string a_ = (actual);                                                    \
		const std::string e_ = (expected);                                                  \
		if (a_ != e_) {                                                                     \
			std::printf("FAIL %s:%d\n  got:      %s\n  expected: %s\n", __FILE__, __LINE__, \
				a_.c_str(), e_.c_str());                                                    \
			++g_failures;                                                                   \
		}                                                                                   \
	} while (0)

// FR-31: the reset confirmation always names the reset level, the returned
// perks and the cap that stays, and asks the reset question.
static void TestResetConfirmAtDefaultCap()
{
	// Cap 100: no gate can have been paid, so no dragon souls are mentioned.
	CHECK_EQ(BM::Texts::ResetConfirm("One-Handed", 15, 100, 0),
		"One-Handed goes back to 15 and its perk points are returned. Its cap stays at 100.\n\n"
		"Are you sure you want to reset this skill?");
}

static void TestResetConfirmRaisedCap()
{
	// A gate paid this climb: its dragon souls are lost.
	CHECK_EQ(BM::Texts::ResetConfirm("One-Handed", 15, 110, 10),
		"One-Handed goes back to 15 and its perk points are returned. Its cap stays at 110, "
		"but the 10 dragon souls spent on it are lost.\n\n"
		"Are you sure you want to reset this skill?");
	// Raised cap, but already reset since: nothing left to lose.
	CHECK_EQ(BM::Texts::ResetConfirm("Archery", 15, 210, 0),
		"Archery goes back to 15 and its perk points are returned. Its cap stays at 210.\n\n"
		"Are you sure you want to reset this skill?");
}

static void TestResetConfirmFollowsResetLevel()
{
	// The level comes from fLegendarySkillResetValue, which a mod may change.
	CHECK_EQ(BM::Texts::ResetConfirm("Alchemy", 30, 100, 0),
		"Alchemy goes back to 30 and its perk points are returned. Its cap stays at 100.\n\n"
		"Are you sure you want to reset this skill?");
}

int main()
{
	TestResetConfirmAtDefaultCap();
	TestResetConfirmRaisedCap();
	TestResetConfirmFollowsResetLevel();
	if (g_failures == 0)
		std::printf("All text tests passed.\n");
	return g_failures == 0 ? 0 : 1;
}
