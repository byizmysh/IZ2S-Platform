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

// A read can have a sector and DMA-read deadline at the same time.
// If both are still in the future, the device callbacks have no work.
static __fi bool iopAdjustFutureCdvdDeadline(u32 pending, u64 cycle, u64& deadline)
{
	constexpr u32 sectorBit = 1u << IopEvt_CdvdSectorReady;
	constexpr u32 readBit = 1u << IopEvt_CdvdRead;
	if ((pending & ~readBit) != sectorBit)
		return false;

	const u64 sectorStart = psxRegs.sCycle[IopEvt_CdvdSectorReady];
	const s32 sectorDelta = psxRegs.eCycle[IopEvt_CdvdSectorReady];
	if (static_cast<s32>(cycle - sectorStart) >= sectorDelta)
		return false;

	// Match each psxSetNextBranch comparison in its original order, keeping
	// the intermediate value in a register. A due read leaves memory alone
	// for the general dispatcher, which can queue or cancel other devices.
	u64 next = deadline;
	if (static_cast<s32>(next - sectorStart) > sectorDelta)
		next = sectorStart + sectorDelta;

	if (pending & readBit)
	{
		const u64 readStart = psxRegs.sCycle[IopEvt_CdvdRead];
		const s32 readDelta = psxRegs.eCycle[IopEvt_CdvdRead];
		if (static_cast<s32>(cycle - readStart) >= readDelta)
			return false;
		if (static_cast<s32>(next - readStart) > readDelta)
			next = readStart + readDelta;
	}

	deadline = next;
	return true;
}

// The synchronous EE owner may also handle a pending CDVD pair if
// neither device callback is due. The IOP's general entry keeps its
// existing empty-queue test in all IOP threading modes.
template <bool AllowFutureCdvd = false>
static __fi bool iopTryScheduleWithoutDeviceEvents()
{
	const u32 pending = psxRegs.interrupt;
	if (!AllowFutureCdvd && pending)
		return false;
	const u64 cycle = psxRegs.cycle;
	const u64 start = psxNextStartCounter;
	const s32 delta = psxNextDeltaCounter;
	if (static_cast<s32>(cycle - start) >= delta)
		return false;
	u64 next = cycle + IopEventWaitCycles;
	// Keep the two signed comparisons separate across the 32-bit wrap.
	if (delta < static_cast<s32>(next - start))
		next = start + delta;
	if constexpr (AllowFutureCdvd)
	{
		// Without a due callback the active-device flag cannot be observed
		// by the EE bridge. Pending IRQs are still handled after publication.
		if (pending && !iopAdjustFutureCdvdDeadline(pending, cycle, next))
			return false;
	}
	psxRegs.iopNextEventCycle = next;
	if (iopCpuInterruptPending())
		iopRaiseCpuInterrupt();
	return true;
}
