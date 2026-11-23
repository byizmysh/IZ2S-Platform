#pragma once

#include "vixl/aarch64/macro-assembler-aarch64.h"

// A power-of-two MainRAM range admits the low segment, KSEG0 and KSEG1.
// The input is the full-width sum of a zero-extended base and signed offset.
inline void EmitPollMainRamSegment(vixl::aarch64::MacroAssembler& masm,
    const vixl::aarch64::Register& effective, const vixl::aarch64::Register& segment,
    std::uint32_t ram_bytes, unsigned access_bytes, vixl::aarch64::Label* reject)
{
    using namespace vixl::aarch64;
    const std::uint64_t bad = ~std::uint64_t(ram_bytes - 1) | (access_bytes - 1);
    Label selected;
    // On the common low-RAM path the result is already a zero segment.
    masm.Ands(segment, effective, bad);
    masm.B(&selected, eq);
    masm.Tst(effective, bad & ~UINT64_C(0xe0000000));
    masm.B(reject, ne);
    masm.Ubfx(segment.W(), effective.W(), 30, 2);
    masm.Cmp(segment.W(), 2);
    masm.B(reject, ne);
    masm.And(segment.W(), effective.W(), 0xe0000000u);
    masm.Bind(&selected);
}

inline void EmitPollMainRamMapCheck(vixl::aarch64::MacroAssembler& masm,
    const vixl::aarch64::Register& raw_map, const vixl::aarch64::Register& main_base,
    const vixl::aarch64::Register& segment, vixl::aarch64::Label* reject)
{
    using namespace vixl::aarch64;
    // Add the validated segment to the vmap bias rather than changing the
    // shared MainRAM base. The segment scratch is dead after this check.
    masm.Add(segment, raw_map, segment);
    masm.Cmp(segment, main_base);
    masm.B(reject, ne);
}
