// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include "IopCounters.h"
#include "IopHw.h"
#include "IopMem.h"
#include "R3000A.h"

inline constexpr u32 IopEventWaitCycles = 384; // Keep in line with EE wait cycle max.

void iopRaiseCpuInterrupt();
#ifdef PS2_IOP_EVENT_FAST_ENTRY
void iopEventTestWithPendingWork();
#endif

static __fi bool iopCpuInterruptPending()
{
	return psxHu32(HW_ICTRL) && (psxHu32(HW_ISTAT) & psxHu32(HW_IMASK)) &&
		(psxRegs.CP0.n.Status & 0xFE01) >= 0x401;
}

// The IOP owner can use this before the full event handler. Counter
// updates and queued devices still go through that handler. The CPU IRQ is checked
// after the deadline is published, just as in the full event handler.
static __fi bool iopTryScheduleWithoutDeviceEvents()
{
	if (psxRegs.interrupt)
		return false;
	const u64 cycle = psxRegs.cycle;
	const u64 start = psxNextStartCounter;
	const s32 delta = psxNextDeltaCounter;
	if (static_cast<s32>(cycle - start) >= delta)
		return false;
	const u64 next = cycle + IopEventWaitCycles;
	// Keep the two signed comparisons separate across the 32-bit wrap.
	psxRegs.iopNextEventCycle = delta < static_cast<s32>(next - start) ? start + delta : next;
	if (iopCpuInterruptPending())
		iopRaiseCpuInterrupt();
	return true;
}
