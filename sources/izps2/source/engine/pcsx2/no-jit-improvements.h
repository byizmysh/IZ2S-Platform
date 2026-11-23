// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Defs.h"
#include <array>

namespace R5900
{
	struct OPCODE;
}

namespace NoJITImprovements
{
	struct alignas(64) EEBlock
	{
		static constexpr u32 InstructionCapacity = 16;
		u32 start_pc = 0;
		u8 instruction_count = 0;
		std::array<u32, InstructionCapacity> code{};
		std::array<const R5900::OPCODE*, InstructionCapacity> opcodes{};
	};

	// Returns a validated block in the current interpreter thread. Read the
	// live count on each iteration because an instruction can invalidate it.
	const EEBlock* GetEEBlock(u32 pc);

	// Drops all cached blocks without releasing the fixed cache allocation.
	void ResetEEBlockCache();

	// Invalidates blocks overlapping an EE word range. Cached bytes are also
	// validated before every reuse, so this is an eager optimization rather
	// than the sole self-modifying-code correctness mechanism.
	void InvalidateEEBlockCache(u32 address, u32 size_in_words);
}
