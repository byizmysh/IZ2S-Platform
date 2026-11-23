#pragma once

#include "vixl/aarch64/macro-assembler-aarch64.h"

// Unmasked, mode-zero V4-16 with WL=1 and no VU memory wrap. The caller
// guarantees the generated destination span is at most 16 KiB.
inline void EmitVifStride16(vixl::aarch64::MacroAssembler& masm, unsigned rows, unsigned stride, bool unsigned_data)
{
    using namespace vixl::aarch64;
    Label overlap, scalar;

    // The paired load reads both source rows before either store. Retain
    // scalar load/store order if the source overlaps the destination span.
    masm.Sub(x2, x0, x1);
    masm.Cmp(x2, rows * 8);
    masm.B(&overlap, lo);
    masm.Sub(x2, x1, x0);
    masm.Cmp(x2, (rows - 1) * stride + 16);
    masm.B(&overlap, lo);

    const auto widen = [&](VRegister output, VRegister input) {
        if (unsigned_data)
            masm.Ushll(output.V4S(), input.V4H(), 0);
        else
            masm.Sshll(output.V4S(), input.V4H(), 0);
    };
    for (unsigned row = 0; row + 1 < rows; row += 2)
    {
        masm.Ldr(q2, MemOperand(x1, row * 8));
        widen(q0, q2);
        if (unsigned_data)
            masm.Ushll2(q1.V4S(), q2.V8H(), 0);
        else
            masm.Sshll2(q1.V4S(), q2.V8H(), 0);
        masm.Str(q0, MemOperand(x0, row * stride));
        masm.Str(q1, MemOperand(x0, (row + 1) * stride));
    }
    if (rows & 1)
    {
        masm.Ldr(d0, MemOperand(x1, (rows - 1) * 8));
        widen(q0, q0);
        masm.Str(q0, MemOperand(x0, (rows - 1) * stride));
    }
    masm.Ret();

    masm.Bind(&overlap);
    masm.Mov(w3, rows);
    masm.Bind(&scalar);
    masm.Ldr(d0, MemOperand(x1, 8, PostIndex));
    widen(q0, q0);
    masm.Str(q0, MemOperand(x0, stride, PostIndex));
    masm.Subs(w3, w3, 1);
    masm.B(&scalar, ne);
    masm.Ret();
}
