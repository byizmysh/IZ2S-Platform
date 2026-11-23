#pragma once

#include "vixl/aarch64/macro-assembler-aarch64.h"

// The caller has already compared at least 64 bytes and advanced both
// pointers equally. Compare the remaining bytes without reading past either
// end. The final vector may overlap bytes that are already in the OR result.
inline bool EmitEeSmcVectorTail(vixl::aarch64::MacroAssembler& masm,
    const vixl::aarch64::Register& live, const vixl::aarch64::Register& expected,
    const vixl::aarch64::VRegister& accumulator, unsigned bytes)
{
    using namespace vixl::aarch64;
    if (bytes != 12 && bytes != 16 && bytes != 28)
        return false;

    const auto merge_difference = [&](int offset) {
        masm.Ldr(q0, MemOperand(live, offset));
        masm.Ldr(q1, MemOperand(expected, offset));
        masm.Eor(q0.V16B(), q0.V16B(), q1.V16B());
        masm.Orr(accumulator.V16B(), accumulator.V16B(), q0.V16B());
    };
    if (bytes > 16)
        merge_difference(0);
    merge_difference(static_cast<int>(bytes) - 16);
    return true;
}
