// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "R3000A.h"
#include "Common.h"

#include "SIO/Sio0.h"
#include "Sif.h"
#include "DebugTools/Breakpoints.h"
#include "R5900OpcodeTables.h"
#include "IopCounters.h"
#include "IopQuietEvent.h"
#include "IopBios.h"
#include "IopHw.h"
#include "IopDma.h"
#include "IopThread/EeBridge.h"
#include "IopThread/OwnerAsserts.h"
#include "CDVD/Ps1CD.h"
#include "CDVD/CDVD.h"
#include "SIO/Sio2.h"
#include "common/ARCADE.h"
#ifdef PS2_IOP_SINGLE_PENDING
#include <bit>
#endif

using namespace R3000A;

R3000Acpu *psxCpu;

// used for constant propagation
u32 g_psxConstRegs[32];
u32 g_psxHasConstReg, g_psxFlushedConstReg;

// Used to signal to the EE when important actions that need IOP-attention have
// happened (hsyncs, vsyncs, IOP exceptions, etc).  IOP runs code whenever this
// is true, even if it's already running ahead a bit.
bool iopEventAction = false;

bool iopEventTestIsActive = false;

alignas(16) psxRegisters psxRegs;

void psxReset()
{
	std::memset(&psxRegs, 0, sizeof(psxRegs));

	psxRegs.pc = 0xbfc00000; // Start in bootstrap
	psxRegs.CP0.n.Status = 0x00400000; // BEV = 1
	psxRegs.CP0.n.PRid   = 0x0000001f; // PRevID = Revision ID, same as the IOP R3000A

	psxRegs.iopBreak = 0;
	psxRegs.iopCycleEE = -1;
	psxRegs.iopCycleEECarry = 0;
	psxRegs.iopNextEventCycle = psxRegs.cycle + 4;

	// An arcade System 256 runs its IOP faster, in step with its EE; its counters are set up with
	// that clock, so it comes first. A console keeps 36.864 MHz, set after.
	if (Arcade::IsActive())
		SetEmulatedClocks(PS2CLK, (PS2CLK == PS2CLK_SS256) ? 55296000 : (PS2CLK == PS2CLK_S256) ? 49152000 : 36864000);
	psxHwReset();
	if (!Arcade::IsActive())
		SetEmulatedClocks(PS2CLK, 36864000);
	ioman::reset();
	psxBiosReset();
}

void psxShutdown() {
	//psxCpu->Shutdown();
}

void psxException(u32 code, u32 bd)
{
//	PSXCPU_LOG("psxException %x: %x, %x", code, psxHu32(0x1070), psxHu32(0x1074));
	//Console.WriteLn("!! psxException %x: %x, %x", code, psxHu32(0x1070), psxHu32(0x1074));
	// Set the Cause
	psxRegs.CP0.n.Cause &= ~0x7f;
	psxRegs.CP0.n.Cause |= code;

	// Set the EPC & PC
	if (bd)
	{
		PSXCPU_LOG("bd set");
		psxRegs.CP0.n.Cause|= 0x80000000;
		psxRegs.CP0.n.EPC = (psxRegs.pc - 4);
	}
	else
		psxRegs.CP0.n.EPC = (psxRegs.pc);

	if (psxRegs.CP0.n.Status & 0x400000)
		psxRegs.pc = 0xbfc00180;
	else
		psxRegs.pc = 0x80000080;

	// Set the Status
	psxRegs.CP0.n.Status = (psxRegs.CP0.n.Status &~0x3f) |
						  ((psxRegs.CP0.n.Status & 0xf) << 2);

	/*if ((((PSXMu32(psxRegs.CP0.n.EPC) >> 24) & 0xfe) == 0x4a)) {
		// "hokuto no ken" / "Crash Bandicot 2" ... fix
		PSXMu32(psxRegs.CP0.n.EPC)&= ~0x02000000;
	}*/

	/*if (psxRegs.CP0.n.Cause == 0x400 && (!(psxHu32(0x1450) & 0x8))) {
		hwIntcIrq(INTC_SBUS);
	}*/
}

__fi void psxSetNextBranch( u64 startCycle, s32 delta )
{
	// typecast the conditional to signed so that things don't blow up
	// if startCycle is greater than our next branch cycle.

	if( (int)(psxRegs.iopNextEventCycle - startCycle) > delta )
		psxRegs.iopNextEventCycle = startCycle + delta;
}

__fi void psxSetNextBranchDelta( s32 delta )
{
	psxSetNextBranch( psxRegs.cycle, delta );
}

__fi int psxTestCycle( u64 startCycle, s32 delta )
{
	// typecast the conditional to signed so that things don't explode
	// if the startCycle is ahead of our current cpu cycle.

	return (int)(psxRegs.cycle - startCycle) >= delta;
}

__fi int psxRemainingCycles(IopEventId n)
{
	if (psxRegs.interrupt & (1 << n))
		return ((psxRegs.cycle - psxRegs.sCycle[n]) + psxRegs.eCycle[n]);
	else
		return 0;
}

__fi void PSX_INT( IopEventId n, s32 ecycle )
{
	IOP_SIDE_ASSERT();

	// 19 is CDVD read int, it's supposed to be high.
	//if (ecycle > 8192 && n != 19)
	//	DevCon.Warning( "IOP cycles high: %d, n %d", ecycle, n );

	psxRegs.interrupt |= 1 << n;

	psxRegs.sCycle[n] = psxRegs.cycle;
	psxRegs.eCycle[n] = ecycle;

	psxSetNextBranchDelta(ecycle);
	// Cast the u32 cycle delta to s32 *before* the float multiply. When `cycle`
	// briefly leads `iopNextEventCycle`, the u32 subtraction underflows to ~4e9;
	// multiplied by ~2.97f the float result is ~1.2e10, out of int range. The
	// cast to int is host-defined: x86 CVTTSS2SI returns INT_MIN, ARM64 FCVTZS
	// saturates to INT_MAX. Casting first lets the multiply happen in signed
	// arithmetic where the small-negative case rounds correctly on both hosts.
	const s32 iopCyclesUntilEvent = static_cast<s32>(psxRegs.iopNextEventCycle - psxRegs.cycle);
	const s32 iopDelta = IopCyclesToEe(iopCyclesUntilEvent);

	if (psxRegs.iopCycleEE < iopDelta)
	{
		// The EE called this int, so inform it to branch as needed:
		
		EeBridge::ScheduleEeEvent(iopDelta - psxRegs.iopCycleEE);
	}
}

static __fi void IopTestEvent( IopEventId n, void (*callback)() )
{
	if( !(psxRegs.interrupt & (1 << n)) ) return;

	if( psxTestCycle( psxRegs.sCycle[n], psxRegs.eCycle[n] ) )
	{
		psxRegs.interrupt &= ~(1 << n);
		callback();
	}
	else
		psxSetNextBranch( psxRegs.sCycle[n], psxRegs.eCycle[n] );
}

static __fi void Sio0TestEvent(IopEventId n)
{
	if (!(psxRegs.interrupt & (1 << n)))
	{
		return;
	}

	if (psxTestCycle(psxRegs.sCycle[n], psxRegs.eCycle[n]))
	{
		psxRegs.interrupt &= ~(1 << n);
		g_Sio0.Interrupt(Sio0Interrupt::TEST_EVENT);
	}
	else
	{
		psxSetNextBranch(psxRegs.sCycle[n], psxRegs.eCycle[n]);
	}
}

#ifdef PS2_IOP_SINGLE_PENDING
static bool s_singlePendingFastPath = false;

void iopSetSinglePendingFastPathForTest(bool enabled)
{
	s_singlePendingFastPath = enabled;
}

static __fi bool iopScheduleSingleFutureEvent(u32 pending)
{
	// Only the event IDs in the ordered dispatch below are eligible. Saved
	// state can contain other bits, which the original dispatcher ignores.
	constexpr u32 supported = (1u << IopEvt_SIF0) | (1u << IopEvt_SIF1) | (1u << IopEvt_SIF2) |
		(1u << IopEvt_SIO) | (1u << IopEvt_CdvdSectorReady) | (1u << IopEvt_CdvdRead) |
		(1u << IopEvt_Cdvd) | (1u << IopEvt_Dma11) | (1u << IopEvt_Dma12) |
		(1u << IopEvt_Cdrom) | (1u << IopEvt_CdromRead) | (1u << IopEvt_DEV9) |
		(1u << IopEvt_USB) | (1u << IopEvt_SIO2) | (1u << IopEvt_Dma8);
	if (!std::has_single_bit(pending) || !(pending & supported))
		return false;

	const auto id = std::countr_zero(pending);
	const u64 start = psxRegs.sCycle[id];
	const s32 delta = psxRegs.eCycle[id];
	if (psxTestCycle(start, delta))
		return false;

	psxSetNextBranch(start, delta);
	return true;
}
#endif

static __fi void _psxTestInterrupts()
{
	u64 next = psxRegs.iopNextEventCycle;
	if (iopAdjustFutureCdvdDeadline(psxRegs.interrupt, psxRegs.cycle, next))
	{
		psxRegs.iopNextEventCycle = next;
		return;
	}

	constexpr u32 secondary = (1u << IopEvt_Cdvd) | (1u << IopEvt_Dma11) | (1u << IopEvt_Dma12) |
		(1u << IopEvt_Cdrom) | (1u << IopEvt_CdromRead) | (1u << IopEvt_DEV9) | (1u << IopEvt_USB) |
		(1u << IopEvt_Dma8) | (1u << IopEvt_SIO2);
#ifdef PS2_IOP_SINGLE_PENDING
	// A future event runs no callback and cannot change the pending set.
	// Due or multiple events still use the original callback order.
	if (s_singlePendingFastPath && iopScheduleSingleFutureEvent(psxRegs.interrupt))
		return;
#endif
	IopTestEvent(IopEvt_SIF0,		sif0Interrupt);	// SIF0
	IopTestEvent(IopEvt_SIF1,		sif1Interrupt);	// SIF1
#ifdef PS2_IOP_SIF_PREFIX
	// Read the live mask after both callbacks. They can queue later events
	// for this pass or leave earlier SIF events pending for the next one.
	constexpr u32 remaining = secondary | (1u << IopEvt_SIF2) | (1u << IopEvt_SIO) |
		(1u << IopEvt_CdvdSectorReady) | (1u << IopEvt_CdvdRead);
	if (!(psxRegs.interrupt & remaining))
		return;
#endif
	IopTestEvent(IopEvt_SIF2,		sif2Interrupt);	// SIF2
	Sio0TestEvent(IopEvt_SIO);
	IopTestEvent(IopEvt_CdvdSectorReady, cdvdSectorReady);
	IopTestEvent(IopEvt_CdvdRead,	cdvdReadInterrupt);

	// Profile-guided Optimization (sorta)
	// The following ints are rarely called.  Encasing them in a conditional
	// as follows helps speed up most games.

	if (psxRegs.interrupt & secondary)
	{
		IopTestEvent(IopEvt_Cdvd,		cdvdActionInterrupt);
		IopTestEvent(IopEvt_Dma11,		psxDMA11Interrupt);	// SIO2
		IopTestEvent(IopEvt_Dma12,		psxDMA12Interrupt);	// SIO2
		IopTestEvent(IopEvt_Cdrom,		cdrInterrupt);
		IopTestEvent(IopEvt_CdromRead,	cdrReadInterrupt);
		IopTestEvent(IopEvt_DEV9,		dev9Interrupt);
		IopTestEvent(IopEvt_USB,		usbInterrupt);
		// Arcade boards only; a console never schedules these.
		IopTestEvent(IopEvt_SIO2,		sio2DelayedInterrupt);
		IopTestEvent(IopEvt_Dma8,		psxDMA8Interrupt);
	}
}

void iopRaiseCpuInterrupt()
{
	PSXCPU_LOG("Interrupt: %x  %x", psxHu32(HW_ISTAT), psxHu32(HW_IMASK));
	psxException(0, 0);
	iopEventAction = true;
}

#ifdef PS2_IOP_EVENT_FAST_ENTRY
[[gnu::noinline]] void iopEventTestWithPendingWork()
#else
__ri void iopEventTest()
#endif
{
	psxRegs.iopNextEventCycle = psxRegs.cycle + IopEventWaitCycles;

	if (psxTestCycle(psxNextStartCounter, psxNextDeltaCounter))
	{
		psxRcntUpdate();
		iopEventAction = true;
	}
	else
	{
		// start the next branch at the next counter event by default
		// the interrupt code below will assign nearer branches if needed.
		if (psxNextDeltaCounter < static_cast<s32>(psxRegs.iopNextEventCycle - psxNextStartCounter))
			psxRegs.iopNextEventCycle = psxNextStartCounter + psxNextDeltaCounter;
	}

	if (psxRegs.interrupt)
	{
		iopEventTestIsActive = true;
		_psxTestInterrupts();
		iopEventTestIsActive = false;
	}

	if (iopCpuInterruptPending())
		iopRaiseCpuInterrupt();
}

#ifdef PS2_IOP_EVENT_FAST_ENTRY
__ri void iopEventTest()
{
	if (!iopTryScheduleWithoutDeviceEvents())
		iopEventTestWithPendingWork();
}
#endif

void iopTestIntc()
{
	if( psxHu32(HW_ICTRL) == 0 ) return;
	if( (psxHu32(HW_ISTAT) & psxHu32(HW_IMASK)) == 0 ) return;

	EeBridge::IopInterruptPending();
}

inline bool psxIsBranchOrJump(u32 addr)
{
	u32 op = iopMemRead32(addr);
	const R5900::OPCODE& opcode = R5900::GetInstruction(op);

	return (opcode.flags & IS_BRANCH) != 0;
}

// The next two functions return 0 if no breakpoint is needed,
// 1 if it's needed on the current pc, 2 if it's needed in the delay slot
// 3 if needed in both

int psxIsBreakpointNeeded(u32 addr)
{
	int bpFlags = 0;
	if (CBreakPoints::IsAddressBreakPoint(BREAKPOINT_IOP, addr))
		bpFlags += 1;

	// there may be a breakpoint in the delay slot
	if (psxIsBranchOrJump(addr) && CBreakPoints::IsAddressBreakPoint(BREAKPOINT_IOP, addr + 4))
		bpFlags += 2;

	return bpFlags;
}

int psxIsMemcheckNeeded(u32 pc)
{
	if (CBreakPoints::GetNumMemchecks() == 0)
		return 0;

	u32 addr = pc;
	if (psxIsBranchOrJump(addr))
		addr += 4;

	u32 op = iopMemRead32(addr);
	const R5900::OPCODE& opcode = R5900::GetInstruction(op);

	if (opcode.flags & IS_MEMORY)
		return addr == pc ? 1 : 2;

	return 0;
}
