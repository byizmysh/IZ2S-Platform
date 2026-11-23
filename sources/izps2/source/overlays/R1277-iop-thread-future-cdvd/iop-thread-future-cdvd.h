#pragma once

#include "IopQuietEvent.h"

// This shortcut is used only on the owner of the isolated IOP. A pending
// counter, device callback or CPU exception stays in the regular dispatcher.
static __fi bool IopThreadFutureCdvdDeadline(u64& next)
{
    const u32 pending = psxRegs.interrupt;
    constexpr u32 sector = 1u << IopEvt_CdvdSectorReady;
    constexpr u32 read = 1u << IopEvt_CdvdRead;
    if ((pending & ~read) != sector)
        return false;

    const u64 cycle = psxRegs.cycle;
    const u64 counter_start = psxNextStartCounter;
    const s32 counter_delta = psxNextDeltaCounter;
    if (static_cast<s32>(cycle - counter_start) >= counter_delta)
        return false;

    u64 candidate = cycle + IopEventWaitCycles;
    if (counter_delta < static_cast<s32>(candidate - counter_start))
        candidate = counter_start + counter_delta;
    if (!iopAdjustFutureCdvdDeadline(pending, cycle, candidate) || iopCpuInterruptPending())
        return false;

    next = candidate;
    return true;
}
