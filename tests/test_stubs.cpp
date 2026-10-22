// Native tests for the hand-written trampolines in src/Stubs.h.
//
// These stubs patch a running game and can never be exercised by the native
// suite, but they CAN be emitted here and inspected: xbyak is header-only and
// generates x86-64 either way. That makes the one class of bug the byte
// signature check cannot catch -- a correct address patched with a wrong
// instruction -- visible without launching Skyrim.

#include "Stubs.h"

#include <cstdio>
#include <cstring>
#include <span>
#include <vector>

static int g_failures = 0;

#define CHECK(cond)                                                     \
	do {                                                                \
		if (!(cond)) {                                                  \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			++g_failures;                                               \
		}                                                               \
	} while (0)

#define CHECK_EQ(a, b)                                                                  \
	do {                                                                                \
		const auto _a = (a), _b = (b);                                                  \
		if (!(_a == _b)) {                                                              \
			std::printf("FAIL %s:%d: %s = %#llx, expected %#llx\n", __FILE__, __LINE__, \
				#a, static_cast<unsigned long long>(_a),                                \
				static_cast<unsigned long long>(_b));                                   \
			++g_failures;                                                               \
		}                                                                               \
	} while (0)

namespace
{
	constexpr std::uintptr_t kFakeCallee = 0x0000'7FFF'1234'5678ull;

	template <class Stub>
	std::vector<std::uint8_t> Emit()
	{
		Stub code{ kFakeCallee };
		code.ready();
		const auto* p = code.getCode();
		return { p, p + code.getSize() };
	}

	// Finds `movss <xmm reg>, [rsp+disp8]` = F3 0F 10 <modrm> 24 <disp8> and
	// returns the disp8 values in order. 0x24 is the SIB byte selecting rsp.
	std::vector<std::uint8_t> MovssRspDisplacements(std::span<const std::uint8_t> a_code)
	{
		std::vector<std::uint8_t> out;
		for (std::size_t i = 0; i + 5 < a_code.size(); ++i) {
			if (a_code[i] == 0xF3 && a_code[i + 1] == 0x0F && a_code[i + 2] == 0x10 &&
				(a_code[i + 3] & 0xC0) == 0x40 && (a_code[i + 3] & 0x07) == 0x04 &&
				a_code[i + 4] == 0x24) {
				out.push_back(a_code[i + 5]);
			}
		}
		return out;
	}

	bool Contains(std::span<const std::uint8_t> a_code, std::initializer_list<std::uint8_t> a_seq)
	{
		const std::vector<std::uint8_t> needle{ a_seq };
		if (needle.size() > a_code.size()) {
			return false;
		}
		for (std::size_t i = 0; i + needle.size() <= a_code.size(); ++i) {
			if (std::memcmp(a_code.data() + i, needle.data(), needle.size()) == 0) {
				return true;
			}
		}
		return false;
	}
}

static void TestFrameAdjustAccountsForTheReturnAddress()
{
	// The invariant the stubs live or die by: our 5-byte call pushes 8 bytes
	// before the stub's own sub rsp, so an operand in the engine's frame is
	// 8 + reserve further away, not reserve.
	CHECK_EQ(BM::Stubs::kStubFrameAdjust, BM::Stubs::kStubReserve + 8u);

	// rsp must be 16-aligned inside the stub: the engine's frame leaves it
	// 16-aligned, the call makes it 8 mod 16, then we subtract the reserve.
	CHECK_EQ((8u + BM::Stubs::kStubReserve) % 16u, 0u);

	// The spill slot must itself be 16-aligned for movaps, and must sit above
	// the 0x20 of shadow space the callee is entitled to scribble on.
	CHECK_EQ(BM::Stubs::kSpillSlot % 16u, 0u);
	CHECK(BM::Stubs::kSpillSlot >= 0x20u);
	CHECK(BM::Stubs::kSpillSlot + 16u <= BM::Stubs::kStubReserve);
}

static void TestXPGainStubReadsTheEngineFrame()
{
	const auto code = Emit<BM::Stubs::XPGainStub>();
	const auto disps = MovssRspDisplacements(code);

	// Two stack reads: ImproveMult and ImproveOffset, both in the engine's
	// frame, so both must be offset by kStubFrameAdjust.
	CHECK_EQ(disps.size(), std::size_t{ 2 });
	if (disps.size() == 2) {
		CHECK_EQ(disps[0], static_cast<std::uint8_t>(
							   BM::Stubs::kStubFrameAdjust + BM::Stubs::kImproveMultSlot));
		CHECK_EQ(disps[1], static_cast<std::uint8_t>(
							   BM::Stubs::kStubFrameAdjust + BM::Stubs::kImproveOffsetSlot));
	}

	// The skill actor value comes from esi, which is callee-saved and so still
	// holds it after the engine's own call at the site.
	CHECK(Contains(code, { 0x89, 0xF1 }));  // mov ecx, esi
	// The points argument is in xmm6 (callee-saved) and goes to arg slot 2.
	CHECK(Contains(code, { 0x0F, 0x28, 0xD6 }));  // movaps xmm2, xmm6
	// Balanced frame and a return.
	CHECK(Contains(code, { 0x48, 0x83, 0xEC, 0x38 }));  // sub rsp, 0x38
	CHECK(Contains(code, { 0x48, 0x83, 0xC4, 0x38 }));  // add rsp, 0x38
	CHECK_EQ(code.back(), std::uint8_t{ 0xC3 });        // ret

	// It must fit the region it replaces via a 5-byte call plus NOPs.
	CHECK(BM::Stubs::kXPGainLength >= 5);
}

static void TestCapStubSpillsAndRestoresXmm0()
{
	const auto code = Emit<BM::Stubs::CapStub>();

	// xmm0 carries the level the following comiss needs and is volatile across
	// a call, so it must be spilled and restored -- in its own frame, so these
	// displacements are NOT adjusted by kStubFrameAdjust.
	CHECK(Contains(code, { 0x0F, 0x29, 0x44, 0x24, 0x20 }));  // movaps [rsp+0x20], xmm0
	CHECK(Contains(code, { 0x0F, 0x28, 0x44, 0x24, 0x20 }));  // movaps xmm0, [rsp+0x20]
	// The cap lands in xmm10, which the comiss reads.
	CHECK(Contains(code, { 0x44, 0x0F, 0x28, 0xD0 }));  // movaps xmm10, xmm0
	CHECK(Contains(code, { 0x89, 0xF1 }));              // mov ecx, esi
	CHECK_EQ(code.back(), std::uint8_t{ 0xC3 });        // ret

	// The stub reads nothing from the engine's frame, so there must be no
	// rsp-relative movss at all -- that is what makes it immune to the
	// return-address mistake.
	CHECK_EQ(MovssRspDisplacements(code).size(), std::size_t{ 0 });

	CHECK(BM::Stubs::kCapLoadLength >= 5);
}

static void TestStubsFitTheTrampolineBudget()
{
	// main.cpp asks SKSE for 256 bytes; both stubs plus their write_call
	// thunks (14 bytes each) have to fit with room to spare.
	const auto cap = Emit<BM::Stubs::CapStub>().size();
	const auto xp = Emit<BM::Stubs::XPGainStub>().size();
	CHECK(cap + xp + 2 * 14 < 256);
}

int main()
{
	TestFrameAdjustAccountsForTheReturnAddress();
	TestXPGainStubReadsTheEngineFrame();
	TestCapStubSpillsAndRestoresXmm0();
	TestStubsFitTheTrampolineBudget();
	if (g_failures == 0) {
		std::printf("All stub tests passed.\n");
	}
	return g_failures == 0 ? 0 : 1;
}
