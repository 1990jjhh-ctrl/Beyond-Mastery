#pragma once

// The hand-written trampolines, separated from Hooks.cpp so they can be
// emitted and inspected natively (tests/test_stubs.cpp). These are the
// riskiest code in the project: a wrong register or a wrong stack
// displacement here is not a compile error and not something the
// byte-signature check can catch, because the signature only proves we
// patched the right *address*.
//
// Stack arithmetic, which is where this is easy to get wrong:
//
//   at the patch site        rsp = S
//   our 5-byte call pushes   rsp = S - 8      <- the return address
//   the stub's sub rsp, R    rsp = S - 8 - R
//
// so an operand the engine writes as [rsp + d] is [rsp + 8 + R + d] inside
// the stub. kStubFrameAdjust is that 8 + R; use it for every operand that
// belongs to the engine's frame.

#include <xbyak/xbyak.h>

#include <cstddef>
#include <cstdint>

namespace BM::Stubs
{
	// Bytes the stub reserves. 0x20 of shadow space for the callee, an
	// aligned 16-byte spill slot at rsp+0x20, and 8 of slack so that
	// rsp % 16 == 0 inside the stub: the engine's frame leaves rsp 16-aligned,
	// the call makes it 8, and 8 - 0x38 is 0 mod 16.
	inline constexpr std::uint32_t kStubReserve = 0x38;

	// Offset from the stub's rsp to the engine's rsp at the patch site.
	inline constexpr std::uint32_t kStubFrameAdjust = 8 + kStubReserve;

	// Where the stub spills a volatile xmm register. 16-aligned by the above.
	inline constexpr std::uint32_t kSpillSlot = 0x20;

	// SKSE trampoline bytes main.cpp asks for: every stub below plus a 14-byte
	// thunk per write_call/write_branch (tests/test_stubs.cpp checks the sum).
	inline constexpr std::size_t kTrampolineSize = 512;

	// --- Cap site (FR-2, FR-8) ----------------------------------------------
	//
	// Replaces `movss xmm10, [rip+100.0]`, 9 bytes, so xmm10 carries this
	// skill's own cap into the comiss that follows. At the patch point esi
	// holds the skill actor value (callee-saved, so it survives our call),
	// xmm0 holds the level the comiss needs (volatile, so it is spilled), and
	// xmm8/xmm10 are callee-saved and need no saving.

	// call [rax+0x18] (3) + movaps xmm8, xmm0 (4)
	inline constexpr std::uint32_t kCapLoadOffset = 7;
	inline constexpr std::size_t   kCapLoadLength = 9;

	struct CapStub : Xbyak::CodeGenerator
	{
		explicit CapStub(std::uintptr_t a_callee)
		{
			sub(rsp, kStubReserve);
			movaps(ptr[rsp + kSpillSlot], xmm0);
			mov(ecx, esi);  // arg 0: the skill actor value
			mov(rax, a_callee);
			call(rax);
			movaps(xmm10, xmm0);  // the cap we just computed
			movaps(xmm0, ptr[rsp + kSpillSlot]);
			add(rsp, kStubReserve);
			ret();
		}
	};

	// --- XP gain site (FR-9) ------------------------------------------------
	//
	// Replaces the three instructions that compute the XP to add:
	//
	//   movss xmm0, [rsp+0x30]   ; ImproveMult, filled by the call just above
	//   mulss xmm0, xmm6         ; the points argument, saved in the prologue
	//   addss xmm0, [rsp+0x34]   ; ImproveOffset
	//
	// 16 bytes, leaving the result in xmm0 exactly as the engine expects.

	inline constexpr std::uint32_t kImproveMultSlot = 0x30;
	inline constexpr std::uint32_t kImproveOffsetSlot = 0x34;
	inline constexpr std::size_t   kXPGainLength = 16;

	struct XPGainStub : Xbyak::CodeGenerator
	{
		explicit XPGainStub(std::uintptr_t a_callee)
		{
			sub(rsp, kStubReserve);
			mov(ecx, esi);  // arg 0: actor value
			// kStubFrameAdjust, not kStubReserve: the 5-byte call at the patch
			// site pushed a return address before this sub rsp, so the
			// engine's frame is a further 8 bytes away.
			movss(xmm1, ptr[rsp + kStubFrameAdjust + kImproveMultSlot]);    // arg 1: ImproveMult
			movaps(xmm2, xmm6);                                             // arg 2: points
			movss(xmm3, ptr[rsp + kStubFrameAdjust + kImproveOffsetSlot]);  // arg 3: ImproveOffset
			mov(rax, a_callee);
			call(rax);  // result in xmm0, where the engine wants it
			add(rsp, kStubReserve);
			ret();
		}
	};

	// --- Barter final clamp (FR-26) -------------------------------------------
	//
	// Replaces the barter menu's 64-byte final clamp (HookSites::kBarterFinalClamp).
	// The engine keeps buy at [rbp+0xA0] and sell at [rbp+0x98]; rbp is its
	// frame register, unaffected by our call. The region directly follows the
	// perk-entry call, which already trashed every volatile register, so code
	// after it can only depend on what the region itself leaves: xmm1 = buy and
	// xmm0 = sell. Both are reloaded after the callee has written them.

	inline constexpr std::uint32_t kBarterBuySlot = 0xA0;
	inline constexpr std::uint32_t kBarterSellSlot = 0x98;

	struct BarterClampStub : Xbyak::CodeGenerator
	{
		// a_callee: void(float* a_buy, float* a_sell)
		explicit BarterClampStub(std::uintptr_t a_callee)
		{
			sub(rsp, kStubReserve);
			lea(rcx, ptr[rbp + kBarterBuySlot]);
			lea(rdx, ptr[rbp + kBarterSellSlot]);
			mov(rax, a_callee);
			call(rax);
			movss(xmm1, ptr[rbp + kBarterBuySlot]);
			movss(xmm0, ptr[rbp + kBarterSellSlot]);
			add(rsp, kStubReserve);
			ret();
		}
	};

	// --- Character XP per skill level-up (FR-11) -------------------------------
	//
	// Replaces `subss xmm1, xmm2; mulss xmm1, [fXPPerSkillRank]` (12 bytes).
	// xmm1 - xmm2 is the new skill level; the stub clamps it to 100 before the
	// multiply. rax is live across the site, so the setting's address goes
	// through rcx, saved and restored; nothing here touches the flags.

	inline constexpr std::size_t kCharXPLength = 12;

	struct CharXPClampStub : Xbyak::CodeGenerator
	{
		// a_xpPerSkillRank: the address of fXPPerSkillRank's float value.
		explicit CharXPClampStub(std::uintptr_t a_xpPerSkillRank)
		{
			Xbyak::Label hundred;
			subss(xmm1, xmm2);
			minss(xmm1, ptr[rip + hundred]);
			push(rcx);
			mov(rcx, a_xpPerSkillRank);
			mulss(xmm1, ptr[rcx]);
			pop(rcx);
			ret();
			L(hundred);
			dd(0x42C80000);  // 100.0f
		}
	};
}
