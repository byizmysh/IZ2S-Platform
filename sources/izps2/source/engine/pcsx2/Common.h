// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Defs.h"

#include <atomic>

static const u32 BIAS = 2;				// Bus is half of the actual ps2 speed
// EE bus clock. A console and a System 246 run at 294.912 MHz; an arcade System 256 runs at 4/3 of
// that and a Super System 256 at 3/2 (PCSX2x6). Only an arcade session ever changes it (VMManager), and
// the IOP clock follows at the same 8:1 ratio (psxReset), so every PS2CLK/PSXCLK ratio stays 8.
static constexpr u32 PS2CLK_DEFAULT = 294912000; //Hz	/* 294.912 MHz: PS2 console, System 246 */
static constexpr u32 PS2CLK_S256 = 393216000;    //Hz	/* 393.216 MHz: System 256 */
static constexpr u32 PS2CLK_SS256 = 442368000;   //Hz	/* 442.368 MHz: Super System 256 */
extern u32 PS2CLK;
extern u32 PSXCLK;	/* 36.864 MHz (49.152 on a System 256, 55.296 on a Super System 256) */
extern std::atomic<u32> IopClockUsesEightToOne;
void SetEmulatedClocks(u32 ee_clock, u32 iop_clock);

static constexpr float IopToEeClockRatio(u32 ee_clock, u32 iop_clock)
{
	// Scaling by a power of two is exact after rounding either clock to float.
	// PS1 mode has a different IOP clock and still uses the actual ratio.
	if (ee_clock && static_cast<u64>(iop_clock) * 8 == ee_clock)
		return 8.0f;
	return static_cast<float>(ee_clock) / static_cast<float>(iop_clock);
}

static inline s32 IopCyclesToEe(s32 cycles)
{
#if defined(__SWITCH2__) && defined(__aarch64__)
	// Keep the original integer-to-float rounding and ARM saturation.
	// The fixed-point conversion scales by eight before truncating.
	if (IopClockUsesEightToOne.load(std::memory_order_relaxed))
	{
		const float rounded_cycles = static_cast<float>(cycles);
		s32 ee_cycles;
		asm("fcvtzs %w0, %s1, #3" : "=r"(ee_cycles) : "w"(rounded_cycles));
		return ee_cycles;
	}
#endif
	return static_cast<s32>(cycles * IopToEeClockRatio(PS2CLK, PSXCLK));
}

#include "Memory.h"
#include "R5900.h"
#include "Hw.h"
#include "Dmac.h"

#include "SaveState.h"
#include "DebugTools/Debug.h"

#include <string>

extern std::string ShiftJIS_ConvertString( const char* src );
extern std::string ShiftJIS_ConvertString( const char* src, int maxlen );
