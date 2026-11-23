#pragma once

#include <cstdint>
#include <cstring>
#include "vixl/aarch64/macro-assembler-aarch64.h"

// Compare 32–44 bytes using two full vectors and, if needed, the last
// 16 bytes. The final vector overlaps the preceding one instead of reading
// past the compared span. The result register is zero only on an exact match.
inline void EmitEeSmcVectorLiteral(vixl::aarch64::MacroAssembler& masm,
    const vixl::aarch64::Register& live, const std::uint8_t* frozen, unsigned bytes,
    const vixl::aarch64::Register& difference)
{
    using namespace vixl::aarch64;
    const auto load_frozen = [&](const VRegister& reg, unsigned offset) {
        std::uint64_t pair[2];
        std::memcpy(pair, frozen + offset, sizeof(pair));
        masm.Ldr(reg, pair[1], pair[0]);
    };

    masm.Ldp(q0, q1, MemOperand(live));
    load_frozen(q2, 0);
    load_frozen(q3, 16);
    masm.Eor(q0.V16B(), q0.V16B(), q2.V16B());
    masm.Eor(q1.V16B(), q1.V16B(), q3.V16B());
    masm.Orr(q0.V16B(), q0.V16B(), q1.V16B());
    if (bytes != 32)
    {
        masm.Ldr(q1, MemOperand(live, bytes - 16));
        load_frozen(q2, bytes - 16);
        masm.Eor(q1.V16B(), q1.V16B(), q2.V16B());
        masm.Orr(q0.V16B(), q0.V16B(), q1.V16B());
    }
    masm.Umaxv(q0.S(), q0.V4S());
    masm.Fmov(difference.W(), q0.S());
}
