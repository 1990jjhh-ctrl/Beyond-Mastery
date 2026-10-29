// Native tests for the hand-written trampolines in src/Stubs.h.
//
// These stubs patch a running game and can never be exercised by the native
// suite, but they CAN be emitted here and inspected: xbyak is header-only and
// generates x86-64 either way. That makes the one class of bug the byte
// signature check cannot catch -- a correct address patched with a wrong
// instruction -- visible without launching Skyrim.

#include "CustomSkillsSites.h"
#include "HookSites.h"
#include "Patterns.h"
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
							   BM::Stubs::kStubFrameAdjust + BM::Stubs::kUseMultSlot));
		CHECK_EQ(disps[1], static_cast<std::uint8_t>(
							   BM::Stubs::kStubFrameAdjust + BM::Stubs::kUseOffsetSlot));
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

static void TestBarterClampStubPointsAtTheEngineFrame()
{
	const auto code = Emit<BM::Stubs::BarterClampStub>();

	// The engine keeps buy at [rbp+0xA0] and sell at [rbp+0x98]; rbp is its
	// frame register and our push of a return address does not move it, so
	// these displacements are NOT adjusted by kStubFrameAdjust.
	CHECK(Contains(code, { 0x48, 0x8D, 0x8D, 0xA0, 0x00, 0x00, 0x00 }));        // lea rcx, [rbp+0xA0]
	CHECK(Contains(code, { 0x48, 0x8D, 0x95, 0x98, 0x00, 0x00, 0x00 }));        // lea rdx, [rbp+0x98]
	CHECK(Contains(code, { 0xF3, 0x0F, 0x10, 0x8D, 0xA0, 0x00, 0x00, 0x00 }));  // movss xmm1, [rbp+0xA0]
	CHECK(Contains(code, { 0xF3, 0x0F, 0x10, 0x85, 0x98, 0x00, 0x00, 0x00 }));  // movss xmm0, [rbp+0x98]
	// The callee gets shadow space and a 16-aligned rsp.
	CHECK(Contains(code, { 0x48, 0x83, 0xEC, 0x38 }));  // sub rsp, 0x38
	CHECK(Contains(code, { 0x48, 0x83, 0xC4, 0x38 }));  // add rsp, 0x38
	CHECK_EQ(code.back(), std::uint8_t{ 0xC3 });        // ret
	CHECK(BM::HookSites::kBarterFinalClampLength >= 5);
}

static void TestCharXPClampStubPreservesRax()
{
	BM::Stubs::CharXPClampStub stub{ kFakeCallee };
	stub.ready();
	const std::vector<std::uint8_t> code{ stub.getCode(), stub.getCode() + stub.getSize() };

	// The new level, clamped to 100, times fXPPerSkillRank -- in that order.
	CHECK(code.size() > 4 && code[0] == 0xF3 && code[1] == 0x0F && code[2] == 0x5C && code[3] == 0xCA);  // subss xmm1, xmm2
	CHECK(Contains(code, { 0xF3, 0x0F, 0x5D, 0x0D }));                                                   // minss xmm1, [rip+...]
	CHECK(Contains(code, { 0xF3, 0x0F, 0x59, 0x09 }));                                                   // mulss xmm1, [rcx]
	// rax is live at the site, so the setting's address goes through rcx,
	// saved and restored.
	CHECK(Contains(code, { 0x51 }));         // push rcx
	CHECK(Contains(code, { 0x59, 0xC3 }));   // pop rcx; ret
	CHECK(Contains(code, { 0x48, 0xB9 }));   // mov rcx, imm64
	CHECK(!Contains(code, { 0x48, 0xB8 }));  // never mov rax, imm64
	// The clamp constant follows the code: 100.0f.
	CHECK(Contains(code, { 0x00, 0x00, 0xC8, 0x42 }));
	CHECK(BM::Stubs::kCharXPLength >= 5);
}

namespace
{
	constexpr std::uintptr_t kFakeBranch = 0x0000'7FFE'0000'1111ull;
	constexpr std::uintptr_t kFakeNext = 0x0000'7FFE'0000'2222ull;
	constexpr std::uintptr_t kFakeFmaf = 0x0000'7FFE'0000'3333ull;

	std::vector<std::uint8_t> Bytes(Xbyak::CodeGenerator& a_code)
	{
		a_code.ready();
		const auto* p = a_code.getCode();
		return { p, p + a_code.getSize() };
	}

	std::vector<std::uint8_t> EmitLimit(const Xbyak::Reg64& a_this, const Xbyak::Reg32& a_level, BM::Stubs::LimitExit a_exit)
	{
		BM::Stubs::LevelLimitStub code{ kFakeCallee, a_this, a_level, a_exit, kFakeBranch, kFakeNext };
		return Bytes(code);
	}

	// Index of the first occurrence of a_seq, or npos.
	std::size_t Find(std::span<const std::uint8_t> a_code, std::initializer_list<std::uint8_t> a_seq)
	{
		const std::vector<std::uint8_t> needle{ a_seq };
		for (std::size_t i = 0; i + needle.size() <= a_code.size(); ++i) {
			if (std::memcmp(a_code.data() + i, needle.data(), needle.size()) == 0) {
				return i;
			}
		}
		return static_cast<std::size_t>(-1);
	}

	std::uint64_t ReadU64(std::span<const std::uint8_t> a_code, std::size_t a_at)
	{
		std::uint64_t v = 0;
		std::memcpy(&v, a_code.data() + a_at, sizeof(v));
		return v;
	}

	// `mov rax, imm64; call rax` for a_target.
	std::vector<std::uint8_t> CallAbs(std::uintptr_t a_target)
	{
		std::vector<std::uint8_t> out{ 0x48, 0xB8 };
		for (int i = 0; i < 8; ++i) {
			out.push_back(static_cast<std::uint8_t>(a_target >> (8 * i)));
		}
		out.insert(out.end(), { 0xFF, 0xD0 });
		return out;
	}

	std::size_t FindVec(std::span<const std::uint8_t> a_code, const std::vector<std::uint8_t>& a_needle)
	{
		for (std::size_t i = 0; i + a_needle.size() <= a_code.size(); ++i) {
			if (std::memcmp(a_code.data() + i, a_needle.data(), a_needle.size()) == 0) {
				return i;
			}
		}
		return static_cast<std::size_t>(-1);
	}

	// Everything the stub runs between its result and its exit: the xmm
	// restores, the frame release and the pops. None of these touch the flags,
	// which is what lets the compare sit before them.
	std::vector<std::uint8_t> LimitRestore()
	{
		std::vector<std::uint8_t> out;
		for (std::uint8_t i = 0; i < 6; ++i) {
			out.insert(out.end(), { 0x0F, 0x28, static_cast<std::uint8_t>(0x44 | (i << 3)), 0x24,
									  static_cast<std::uint8_t>(BM::Stubs::kLimitSpill + 16 * i) });
		}
		out.insert(out.end(), { 0x48, 0x8D, 0xA4, 0x24, BM::Stubs::kLimitFrame, 0x00, 0x00, 0x00 });  // lea rsp, [rsp+frame]
		out.insert(out.end(), { 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x58 });  // pop r11 ... rax
		return out;
	}

	void CheckLimitSaves(std::span<const std::uint8_t> a_code)
	{
		// push rax, rcx, rdx, r8, r9, r10, r11, then the frame.
		const std::vector<std::uint8_t> prologue{ 0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53,
			0x48, 0x81, 0xEC, BM::Stubs::kLimitFrame, 0x00, 0x00, 0x00 };
		CHECK_EQ(FindVec(a_code, prologue), std::size_t{ 0 });
		for (std::uint8_t i = 0; i < 6; ++i) {  // movaps [rsp+spill+16i], xmm_i
			CHECK(FindVec(a_code, { 0x0F, 0x29, static_cast<std::uint8_t>(0x44 | (i << 3)), 0x24,
									  static_cast<std::uint8_t>(BM::Stubs::kLimitSpill + 16 * i) }) != static_cast<std::size_t>(-1));
		}
		CHECK(FindVec(a_code, LimitRestore()) != static_cast<std::size_t>(-1));
	}
}

static void TestLevelLimitStubFrame()
{
	// Entered by a jmp from a frame with rsp 16-aligned (both CSF functions
	// have an even number of 8-byte pushes plus the return address and a
	// 16-multiple sub): the seven pushes and the frame must keep it so, and
	// the xmm spill must sit above the shadow space, 16-aligned.
	CHECK_EQ((7u * 8u + BM::Stubs::kLimitFrame) % 16u, 0u);
	CHECK_EQ(BM::Stubs::kLimitSpill % 16u, 0u);
	CHECK(BM::Stubs::kLimitSpill >= 0x20u);
	CHECK(BM::Stubs::kLimitSpill + 6u * 16u <= BM::Stubs::kLimitFrame);
}

static void TestLevelLimitStubBranchIfReached()
{
	using namespace Xbyak::util;
	// Skill::Advance's loop entry: cmp esi, limit; jge exit.
	const auto code = EmitLimit(r14, esi, BM::Stubs::LimitExit::kBranchIfReached);
	CheckLimitSaves(code);

	// call: rcx = this->Level, then the callee.
	const auto load = FindVec(code, { 0x49, 0x8B, 0x4E, BM::CustomSkillsSites::kLevelMember });  // mov rcx, [r14+0x40]
	const auto call = FindVec(code, CallAbs(kFakeCallee));
	CHECK(load != static_cast<std::size_t>(-1));
	CHECK(call != static_cast<std::size_t>(-1) && load < call);

	// compare, straight after the call, then only flag-neutral restores until
	// the jge.
	auto expected = CallAbs(kFakeCallee);
	expected.insert(expected.end(), { 0x39, 0xC6 });  // cmp esi, eax
	const auto restore = LimitRestore();
	expected.insert(expected.end(), restore.begin(), restore.end());
	expected.push_back(0x7D);  // jge rel8
	CHECK(FindVec(code, expected) != static_cast<std::size_t>(-1));

	// exits: jge to the branch target, otherwise on to the next instruction.
	CHECK(code.size() > 16);
	CHECK_EQ(ReadU64(code, code.size() - 16), std::uint64_t{ kFakeNext });
	CHECK_EQ(ReadU64(code, code.size() - 8), std::uint64_t{ kFakeBranch });
	// jge +6 skips `jmp [rip+8]` (to next) and lands on `jmp [rip+8]` (to branch).
	CHECK(Find(code, { 0x7D, 0x06, 0xFF, 0x25, 0x06, 0x00, 0x00, 0x00, 0xFF, 0x25, 0x08, 0x00, 0x00, 0x00 }) ==
		  code.size() - 16 - 14);
}

static void TestLevelLimitStubBranchIfBelow()
{
	using namespace Xbyak::util;
	// Skill::Advance's loop repeat: cmp esi, limit; jl loop top.
	const auto code = EmitLimit(r14, esi, BM::Stubs::LimitExit::kBranchIfBelow);
	CheckLimitSaves(code);
	CHECK(FindVec(code, { 0x49, 0x8B, 0x4E, 0x40 }) != static_cast<std::size_t>(-1));  // mov rcx, [r14+0x40]
	auto expected = CallAbs(kFakeCallee);
	expected.insert(expected.end(), { 0x39, 0xC6 });  // cmp esi, eax
	const auto restore = LimitRestore();
	expected.insert(expected.end(), restore.begin(), restore.end());
	expected.insert(expected.end(), { 0x7C, 0x06 });  // jl rel8 +6
	CHECK(FindVec(code, expected) != static_cast<std::size_t>(-1));
	CHECK_EQ(ReadU64(code, code.size() - 16), std::uint64_t{ kFakeNext });
	CHECK_EQ(ReadU64(code, code.size() - 8), std::uint64_t{ kFakeBranch });
}

static void TestLevelLimitStubIncrementCheck()
{
	using namespace Xbyak::util;
	// Skill::Increment: this in rdi, the level in r14d; cmp r14d, limit; jge return.
	const auto code = EmitLimit(rdi, r14d, BM::Stubs::LimitExit::kBranchIfReached);
	CheckLimitSaves(code);
	CHECK(FindVec(code, { 0x48, 0x8B, 0x4F, 0x40 }) != static_cast<std::size_t>(-1));  // mov rcx, [rdi+0x40]
	auto expected = CallAbs(kFakeCallee);
	expected.insert(expected.end(), { 0x41, 0x39, 0xC6 });  // cmp r14d, eax
	const auto restore = LimitRestore();
	expected.insert(expected.end(), restore.begin(), restore.end());
	expected.insert(expected.end(), { 0x7D, 0x06 });  // jge rel8 +6
	CHECK(FindVec(code, expected) != static_cast<std::size_t>(-1));
	CHECK_EQ(ReadU64(code, code.size() - 16), std::uint64_t{ kFakeNext });
	CHECK_EQ(ReadU64(code, code.size() - 8), std::uint64_t{ kFakeBranch });
}

static void TestLevelLimitStubRemaining()
{
	using namespace Xbyak::util;
	// Skill::Increment's count: eax = limit - r14d, every other volatile
	// register restored, then on to the next instruction.
	const auto code = EmitLimit(rdi, r14d, BM::Stubs::LimitExit::kRemaining);
	CheckLimitSaves(code);
	CHECK(FindVec(code, { 0x48, 0x8B, 0x4F, 0x40 }) != static_cast<std::size_t>(-1));  // mov rcx, [rdi+0x40]
	auto expected = CallAbs(kFakeCallee);
	expected.insert(expected.end(), { 0x44, 0x29, 0xF0 });  // sub eax, r14d
	// The result replaces rax's saved copy, so the pop of rax delivers it.
	expected.insert(expected.end(), { 0x48, 0x89, 0x84, 0x24, static_cast<std::uint8_t>(BM::Stubs::kLimitFrame + 6 * 8), 0x00, 0x00, 0x00 });
	const auto restore = LimitRestore();
	expected.insert(expected.end(), restore.begin(), restore.end());
	expected.insert(expected.end(), { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 });  // jmp [rip]
	CHECK(FindVec(code, expected) != static_cast<std::size_t>(-1));
	// One exit only.
	CHECK_EQ(ReadU64(code, code.size() - 8), std::uint64_t{ kFakeNext });
	CHECK(FindVec(code, { 0x39, 0xC6 }) == static_cast<std::size_t>(-1));  // no compare
}

static void TestUseXPFactorStub()
{
	using namespace Xbyak::util;
	BM::Stubs::UseXPFactorStub stub{ kFakeCallee, kFakeFmaf, r14 };
	const auto                 code = Bytes(stub);

	// save: entered by a call, so the usual reserve keeps rsp 16-aligned; the
	// fmaf result is the one live volatile value and is spilled across the call.
	CHECK_EQ(Find(code, { 0x48, 0x83, 0xEC, 0x38 }), std::size_t{ 0 });  // sub rsp, 0x38
	// The replaced call first, then its result spilled.
	auto expected = CallAbs(kFakeFmaf);
	expected.insert(expected.end(), { 0xF3, 0x0F, 0x11, 0x44, 0x24, BM::Stubs::kSpillSlot });  // movss [rsp+0x20], xmm0
	CHECK(FindVec(code, expected) != static_cast<std::size_t>(-1));
	// call: rcx = this->Level, then the callee.
	expected = { 0x49, 0x8B, 0x4E, 0x40 };  // mov rcx, [r14+0x40]
	const auto callee = CallAbs(kFakeCallee);
	expected.insert(expected.end(), callee.begin(), callee.end());
	expected.insert(expected.end(), { 0xF3, 0x0F, 0x59, 0x44, 0x24, BM::Stubs::kSpillSlot });  // mulss xmm0, [rsp+0x20]
	expected.insert(expected.end(), { 0x48, 0x83, 0xC4, 0x38, 0xC3 });                         // add rsp, 0x38; ret
	CHECK(FindVec(code, expected) != static_cast<std::size_t>(-1));
	CHECK_EQ(code.back(), std::uint8_t{ 0xC3 });
}

static void TestCustomSkillsSitesFit()
{
	using namespace BM::CustomSkillsSites;
	// Each site holds the 5-byte jmp or call, and its pattern covers it.
	for (const auto* site : kLimitSites) {
		CHECK(site->length >= 5);
		CHECK(site->length <= BM::Patterns::Length(site->pattern));
	}
	CHECK_EQ(kAdvanceUseXP.length, std::size_t{ 5 });
	CHECK(BM::Patterns::Length(kAdvanceUseXP.pattern) >= 5);
}

static void TestCustomSkillsTrampolineBudget()
{
	using namespace Xbyak::util;
	using BM::Stubs::LimitExit;
	// The CSF trampoline holds the stubs only: the sites are patched with a
	// direct rel32 to each.
	BM::Stubs::UseXPFactorStub xp{ kFakeCallee, kFakeFmaf, r14 };
	const auto                 total = EmitLimit(r14, esi, LimitExit::kBranchIfReached).size() +
	                                   EmitLimit(r14, esi, LimitExit::kBranchIfBelow).size() +
	                                   EmitLimit(rdi, r14d, LimitExit::kBranchIfReached).size() +
	                                   EmitLimit(rdi, r14d, LimitExit::kRemaining).size() + Bytes(xp).size();
	CHECK(total <= BM::Stubs::kCustomSkillsTrampolineSize);
}

static void TestStubsFitTheTrampolineBudget()
{
	// main.cpp asks SKSE for kTrampolineSize bytes; every stub plus every
	// write_call/write_branch thunk (14 bytes each) has to fit with room to spare.
	const auto stubs = Emit<BM::Stubs::CapStub>().size() + Emit<BM::Stubs::XPGainStub>().size() +
	                   Emit<BM::Stubs::BarterClampStub>().size() + Emit<BM::Stubs::CharXPClampStub>().size();
	// Thunks: cap, XP gain, two Legendary boxes, three skills-menu calls,
	// the crafting-bar skill read, the GetClampedActorValue branch, two
	// spell-cost calls, the charge call, the barter clamp and the
	// character-XP clamp.
	CHECK(stubs + 14 * 14 < BM::Stubs::kTrampolineSize);
}

int main()
{
	TestFrameAdjustAccountsForTheReturnAddress();
	TestXPGainStubReadsTheEngineFrame();
	TestCapStubSpillsAndRestoresXmm0();
	TestBarterClampStubPointsAtTheEngineFrame();
	TestCharXPClampStubPreservesRax();
	TestStubsFitTheTrampolineBudget();
	TestLevelLimitStubFrame();
	TestLevelLimitStubBranchIfReached();
	TestLevelLimitStubBranchIfBelow();
	TestLevelLimitStubIncrementCheck();
	TestLevelLimitStubRemaining();
	TestUseXPFactorStub();
	TestCustomSkillsSitesFit();
	TestCustomSkillsTrampolineBudget();
	if (g_failures == 0) {
		std::printf("All stub tests passed.\n");
	}
	return g_failures == 0 ? 0 : 1;
}
