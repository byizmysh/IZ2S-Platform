// SPDX-FileCopyrightText: 2026 yaps2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// ARM64 EE Jump Instruction Codegen

#include "arm64/iR5900-arm64.h"
#include "Config.h"
#include "vtlb.h"
#include "common/Console.h"

namespace a64 = vixl::aarch64;

namespace R5900 {
namespace Dynarec {
namespace OpcodeImpl {

namespace Interp = R5900::Interpreter::OpcodeImpl;


/*********************************************************
 * Jump to target                                        *
 * Format: OP target                                     *
 *********************************************************/

//// J
void recJ()
{
	u32 newpc = (_InstrucTarget_ << 2) + (pc & 0xf0000000);
	recompileNextInstruction(true, false);
	if (EmuConfig.Gamefixes.GoemonTlbHack)
		SetBranchImm(vtlb_V2P(newpc));
	else
		SetBranchImm(newpc);
}

//// JAL — jump and link (r31 = return address)
void recJAL()
{
	u32 newpc = (_InstrucTarget_ << 2) + (pc & 0xf0000000);
	const u32 retpc = pc + 4; // == the $ra link value; capture before the delay slot advances pc
	_deleteEEreg(31, 0);
	if (EE_CONST_PROP)
	{
		GPR_SET_CONST(31);
		g_cpuConstRegs[31].UL[0] = retpc;
		g_cpuConstRegs[31].UL[1] = 0;
	}
	else
	{
		armAsm->Mov(RXSCRATCH, (u64)retpc);
		_eeStoreGPRDestReg(31, RXSCRATCH);
	}

	recompileNextInstruction(true, false);
	if (EmuConfig.Gamefixes.GoemonTlbHack)
		SetBranchImm(vtlb_V2P(newpc));
	else
		SetBranchImmCall(newpc, retpc);
}

/*********************************************************
 * Register jump                                         *
 * Format: OP rs, rd                                     *
 *********************************************************/

#ifdef PS2_EE_PINNED_BRANCH_TARGET
static const a64::Register* recPinnedBranchTarget(u32 guest, bool swapped)
{
	if (!swapped)
		return nullptr;
	const auto* pin = armEEPinForGPR(guest);
	if (pin && !GPR_IS_CONST1(guest) && !_hasNEONreg(NEONTYPE_GPRREG, guest))
		return pin;
	return nullptr;
}
#endif

//// JR — jump to address in rs
void recJR()
{
	const u32 rs = _Rs_;

	// A delay slot that neither writes rs nor needs the branch's own ordering
	// is emitted ahead of the jump instead, which takes it out of the window
	// the capture below has to survive.
	const bool swapped = !EmuConfig.Gamefixes.GoemonTlbHack && TrySwapDelaySlot(rs, 0, 0, true);
	// JR $ra pairs with the call-ret ring. Goemon's translated targets
	// cannot match that ring's virtual return addresses.
	const auto mode = rs == 31 && !EmuConfig.Gamefixes.GoemonTlbHack
		? EEBranchRegMode::Return : EEBranchRegMode::Jump;
#ifdef PS2_EE_PINNED_BRANCH_TARGET
	if (const auto* pin = recPinnedBranchTarget(rs, swapped))
	{
		SetBranchRegFromPin(mode, 0, *pin);
		return;
	}
#endif

	// Capture the jump target before the delay slot, which MIPS lets write rs,
	// into an ARM64TYPE_PCWRITEBACK slot -- see SetBranchReg for how it's read
	// back.
	const int wbreg = _allocArm64GPR(ARM64TYPE_PCWRITEBACK, 0, MODE_WRITE);
	_eeMoveGPRtoR(armWRegister(wbreg), rs);

	if (!swapped)
		recompileNextInstruction(true, false);

	SetBranchReg(mode, 0, wbreg);
}

//// JALR — jump to rs, link in rd
void recJALR()
{
	const u32 rs = _Rs_;
	const u32 rd = _Rd_;
	const u32 newpc = pc + 4; // captured before a swap can advance pc past the slot

	// See recJR. rd joins rs in the safety check because the link is written
	// before the delay slot runs, so a slot that reads or writes rd cannot be
	// hoisted past that write. The rd == rs term is x86's, kept for parity: it
	// is what stops x86's swapped arm from reading rs after the link write
	// (iR5900Jump.cpp:174).
	const bool swapped = !EmuConfig.Gamefixes.GoemonTlbHack && rd != rs &&
		TrySwapDelaySlot(rs, 0, rd, true);
	const a64::Register* pin = nullptr;
#ifdef PS2_EE_PINNED_BRANCH_TARGET
	pin = recPinnedBranchTarget(rs, swapped);
#endif

	// Capture the jump target before the delay slot; see recJR. Ordered ahead
	// of the rd write below because rd may be rs, and the capture has to see
	// the pre-link value.
	int wbreg = -1;
	if (!pin)
	{
		wbreg = _allocArm64GPR(ARM64TYPE_PCWRITEBACK, 0, MODE_WRITE);
		_eeMoveGPRtoR(armWRegister(wbreg), rs);
	}

	// Write link address to rd
	if (rd)
	{
		_deleteEEreg(rd, 0);
		if (EE_CONST_PROP)
		{
			GPR_SET_CONST(rd);
			g_cpuConstRegs[rd].UL[0] = newpc;
			g_cpuConstRegs[rd].UL[1] = 0;
		}
		else
		{
			armAsm->Mov(RXSCRATCH, (u64)newpc);
			_eeStoreGPRDestReg(rd, RXSCRATCH);
		}
	}

	if (!swapped)
		recompileNextInstruction(true, false);

	// JALR linking into $ra is the ABI indirect-call idiom — push a call-ret
	// frame and transfer via BL so the callee's return RETs to our landing.
	// Other link registers don't pair with the JR-$ra pop, so they take the
	// plain jump (their returns just compare-miss if the callee uses jr $ra).
	const auto mode = rd == 31 && !EmuConfig.Gamefixes.GoemonTlbHack
		? EEBranchRegMode::Call : EEBranchRegMode::Jump;
	const u32 call_return_pc = mode == EEBranchRegMode::Call ? newpc : 0;
#ifdef PS2_EE_PINNED_BRANCH_TARGET
	if (pin)
	{
		SetBranchRegFromPin(mode, call_return_pc, *pin);
		return;
	}
#endif
	SetBranchReg(mode, call_return_pc, wbreg);
}


} // namespace OpcodeImpl
} // namespace Dynarec
} // namespace R5900
