// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include "IopCounters.h"
#include "IopHw.h"
#include "IopMem.h"
#include "R3000A.h"

#include <cstring>

inline constexpr u32 IopEventWaitCycles = 384; // Keep in line with EE wait cycle max.

void iopRaiseCpuInterrupt();
#ifdef PS2_IOP_EVENT_FAST_ENTRY
void iopEventTestWithPendingWork();
#endif

static __fi bool iopCpuInterruptPending()
{
	if (!psxHu32(HW_ICTRL))
		return false;
	static_assert(HW_IMASK == HW_ISTAT + sizeof(u32) && HW_ISTAT % alignof(u64) == 0);
	// Read the two adjacent backing slots as one snapshot. This reads
	// ordinary IOP memory, without invoking guest MMIO read semantics.
	u64 irq;
#if defined(__clang__)
	__builtin_memcpy_inline(&irq, &iopHw[HW_ISTAT & 0xffff], sizeof(irq));
#else
	std::memcpy(&irq, &iopHw[HW_ISTAT & 0xffff], sizeof(irq));
#endif
	return (static_cast<u32>(irq) & static_cast<u32>(irq >> 32)) &&
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
