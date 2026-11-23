// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include "Config.h"
#include "IopHw.h"
#include "IopMem.h"
#include "R3000A.h"
#include "arm64/BaseblockEx-arm64.h"

struct IopOwnerIdleSlice
{
	const BASEBLOCK* slot;
	uptr target;
	u32 pc;
};

// Written by the Off-mode IOP compiler after publication, and consumed by
// the same owner. Reset before the LUT or emitted host addresses are reused.
extern IopOwnerIdleSlice iopOwnerIdleSlice;

static __fi bool iopTryOwnerIdleSlice(s32& budget)
{
	const auto& idle = iopOwnerIdleSlice;
	if (psxRegs.pc != idle.pc || !idle.slot || psxCpu != &psxRec ||
		(static_cast<u32>(budget) - 1) >= INT32_MAX - 7 ||
		!EmuConfig.Speedhacks.WaitLoop || (psxHu32(HW_ICFG) & 8) ||
		idle.slot->GetFnptr() != idle.target)
		return false;

	const u32 advance = (static_cast<u32>(budget) + 7) / 8;
	const u64 cycle = psxRegs.cycle;
	const u64 deadline = psxRegs.iopNextEventCycle;
	if (cycle > deadline || advance > deadline - cycle)
		return false;

	budget -= static_cast<s32>(advance * 8);
	psxRegs.cycle = cycle + advance;
	psxRegs.iopBreak = 0;
	psxRegs.iopCycleEE = budget;
	return true;
}
