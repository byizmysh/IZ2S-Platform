// SPDX-FileCopyrightText: 2026 yaps2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

//------------------------------------------------------------------
// Micro VU - Pass 2 Functions (ARM64)
//------------------------------------------------------------------

//------------------------------------------------------------------
// Flag Allocators
//------------------------------------------------------------------

__fi static const a64::Register& getFlagReg(uint fInst)
{
	static const a64::Register* const gprFlags[4] = {&gprF0, &gprF1, &gprF2, &gprF3};
	pxAssert(fInst < 4);
	return *gprFlags[fInst];
}

__fi void mVUallocSFLAGa(const a64::Register& reg, int fInstance)
{
	armAsm->Mov(reg.W(), getFlagReg(fInstance));
}

__fi void mVUallocSFLAGb(const a64::Register& reg, int fInstance)
{
	armAsm->Mov(getFlagReg(fInstance), reg.W());
}

// Normalize Status Flag (denormalized → standard format)
// Extracts Z, S, ZS, SS bits and shifts DS/DI/OS/US/D/I/O/U bits
__ri void mVUallocSFLAGc(const a64::Register& reg, int fInstance, u32 needed = 0x3ffffu)
{
	const a64::Register& stored = getFlagReg(fInstance);
	pxAssert(!reg.Is(stored));
	const bool needUpper = (needed & 0x3fffcu) != 0;
	if (needUpper)
	{
		armAsm->Lsr(reg.W(), stored.W(), 16);
		armAsm->Lsl(reg.W(), reg.W(), 2);
	}
	else
	{
		armAsm->Mov(reg.W(), 0);
	}

	const auto setBit = [&](u32 input, u32 result) {
		if ((needed & result) == 0)
			return;
		armAsm->Tst(stored.W(), input);
		a64::Label skip;
		armAsm->B(&skip, a64::eq);
		armAsm->Orr(reg.W(), reg.W(), result);
		armAsm->Bind(&skip);
	};
	setBit(0x0f00, 0x0001);
	setBit(0xf000, 0x0002);
	setBit(0x000f, 0x0040);
	setBit(0x00f0, 0x0080);
}

// Denormalize Status Flag (standard → denormalized format)
__ri void mVUallocSFLAGd(u32* memAddr, const a64::Register& reg = a64::w9,
                          const a64::Register& tmp1 = a64::w10, const a64::Register& tmp2 = a64::w11)
{
	armMoveAddressToReg(a64::x8, memAddr);
	armAsm->Ldr(tmp2.W(), a64::MemOperand(a64::x8));

	// reg = (tmp2 >> 3) & 0x18
	armAsm->Lsr(reg.W(), tmp2.W(), 3);
	armAsm->And(reg.W(), reg.W(), 0x18);

	// tmp1 = (tmp2 << 11) & 0x1800
	armAsm->Lsl(tmp1.W(), tmp2.W(), 11);
	armAsm->And(tmp1.W(), tmp1.W(), 0x1800);
	armAsm->Orr(reg.W(), reg.W(), tmp1.W());

	// tmp2 = (tmp2 << 14) & 0x3cf0000
	armAsm->Lsl(tmp2.W(), tmp2.W(), 14);
	armAsm->And(tmp2.W(), tmp2.W(), 0x3cf0000);
	armAsm->Orr(reg.W(), reg.W(), tmp2.W());
}

//------------------------------------------------------------------
// MAC/Clip Flag Allocators
//------------------------------------------------------------------

__fi void mVUallocMFLAGa(mV, const a64::Register& reg, int fInstance)
{
	if (mVUresidentFlagRings(mVU) && fInstance < 4)
		armAsm->Umov(reg.W(), a64::q23.V8H(), fInstance * 2);
	else
		armAsm->Ldrh(reg.W(), mVUmacFlagMem(fInstance));
}

__fi void mVUallocMFLAGb(mV, const a64::Register& reg, int fInstance)
{
	if (fInstance < 4 && mVUresidentFlagRings(mVU))
		armAsm->Ins(a64::q23.V4S(), fInstance, reg.W());
	else if (fInstance < 4)
		armAsm->Str(reg.W(), mVUmacFlagMem(fInstance));
	else
		armAsm->Str(reg.W(), mVUstateMem(offsetof(VURegs, VI) + REG_MAC_FLAG * sizeof(REG_VI)));
}

__fi void mVUallocCFLAGa(mV, const a64::Register& reg, int fInstance)
{
	if (fInstance < 4 && mVUresidentFlagRings(mVU))
		armAsm->Umov(reg.W(), a64::q24.V4S(), fInstance);
	else if (fInstance < 4)
		armAsm->Ldr(reg.W(), mVUclipFlagMem(fInstance));
	else
		armAsm->Ldr(reg.W(), mVUstateMem(offsetof(VURegs, VI) + REG_CLIP_FLAG * sizeof(REG_VI)));
}

__fi void mVUallocCFLAGb(mV, const a64::Register& reg, int fInstance)
{
	if (fInstance < 4 && mVUresidentFlagRings(mVU))
		armAsm->Ins(a64::q24.V4S(), fInstance, reg.W());
	else if (fInstance < 4)
		armAsm->Str(reg.W(), mVUclipFlagMem(fInstance));
	else
		armAsm->Str(reg.W(), mVUstateMem(offsetof(VURegs, VI) + REG_CLIP_FLAG * sizeof(REG_VI)));
}

//------------------------------------------------------------------
// P/Q Reg Allocators
//------------------------------------------------------------------

// Get P register value from qmmPQ (lane 2 or 3 based on readP instance)
__fi void getPreg(mV, const a64::VRegister& reg)
{
	// qmmPQ layout: [0]=Q, [1]=pending_q, [2]=P, [3]=pending_p
	int lane = 2 + mVUinfo.readP;
	mVUunpack_xyzw(reg, qmmPQ, lane);
}

// Get Q register value from qmmPQ (lane 0 or 1 based on qInstance)
__fi void getQreg(const a64::VRegister& reg, int qInstance)
{
	mVUunpack_xyzw(reg, qmmPQ, qInstance);
}

// Write Q register value back into qmmPQ
__ri void writeQreg(const a64::VRegister& reg, int qInstance)
{
	// Insert scalar from reg lane 0 into qmmPQ at qInstance lane
	armAsm->Ins(qmmPQ.V4S(), qInstance, reg.V4S(), 0);
}

// VI Backup (writeVIBackup) is defined in microVU_IR-arm64.h
