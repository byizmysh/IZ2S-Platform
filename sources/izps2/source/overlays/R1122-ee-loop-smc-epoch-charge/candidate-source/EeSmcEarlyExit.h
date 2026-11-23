#pragma once

#include <cstdint>
#include <optional>

#include "vixl/aarch64/macro-assembler-aarch64.h"

struct EeSmcExitRule
{
    unsigned source;
    unsigned compare;
    unsigned mask;
    unsigned prefixWords;
    bool equal;
};

inline bool EeSmcExitSafeDelaySlot(std::uint32_t code)
{
    // Only GPR operations that cannot trap or observe memory. The branch
    // itself still executes the delay slot with the normal compiler.
    switch (code >> 26)
    {
        case 0x00:
            switch (code & 63)
            {
                case 0x00: case 0x02: case 0x03: // SLL, SRL, SRA
                case 0x21: case 0x23: // ADDU, SUBU
                case 0x24: case 0x25: case 0x26: case 0x27: // AND, OR, XOR, NOR
                case 0x2a: case 0x2b: // SLT, SLTU
                case 0x2d: case 0x2f: // DADDU, DSUBU
                    return true;
                default:
                    return false;
            }
        case 0x09: case 0x0a: case 0x0b: // ADDIU, SLTI, SLTIU
        case 0x0c: case 0x0d: case 0x0e: case 0x0f: // ANDI, ORI, XORI, LUI
        case 0x19: // DADDIU
            return true;
        default:
            return false;
    }
}

inline std::optional<EeSmcExitRule> DecodeEeSmcExit(
    const std::uint32_t (&code)[3], std::uint32_t pc, std::uint32_t end)
{
    const bool masked = code[0] >> 26 == 0x0c;
    const unsigned branchIndex = masked ? 1 : 0;
    const std::uint32_t branch = code[branchIndex];
    const unsigned op = branch >> 26;
    if ((op != 4 && op != 5) || !EeSmcExitSafeDelaySlot(code[branchIndex + 1]))
        return std::nullopt;

    const unsigned displacement = branch & 0xffff;
    const std::uint64_t target = static_cast<std::uint64_t>(pc) + branchIndex * 4 + 4 + displacement * 4;
    if (!displacement || displacement > 0x7fff || target < end || target > UINT32_MAX)
        return std::nullopt;

    const unsigned first = (branch >> 21) & 31;
    const unsigned second = (branch >> 16) & 31;
    if (first == second)
        return std::nullopt;
    if (!masked)
        return EeSmcExitRule{first ? first : second, first ? second : 0, 0, 2, op == 4};

    const unsigned destination = (code[0] >> 16) & 31;
    const unsigned mask = code[0] & 0xffff;
    if (!destination || (first != destination && second != destination) ||
        !vixl::aarch64::Assembler::IsImmLogical(mask, 32))
        return std::nullopt;
    return EeSmcExitRule{(code[0] >> 21) & 31, first == destination ? second : first, mask, 3, op == 4};
}

inline void EmitEeSmcExitPrefix(vixl::aarch64::MacroAssembler& masm,
    const vixl::aarch64::Register& live, const std::uint32_t (&code)[3],
    unsigned prefixWords, vixl::aarch64::Label* mismatch)
{
    using namespace vixl::aarch64;
    const auto expected = static_cast<std::uint64_t>(code[1]) << 32 | code[0];
    masm.Ldr(x9, MemOperand(live));
    masm.Ldr(x2, expected);
    masm.Cmp(x9, x2);
    if (prefixWords == 3)
    {
        masm.Ldr(w9, MemOperand(live, 8));
        if (code[2] <= 31)
            masm.Ccmp(w9, code[2], NoFlag, eq);
        else
        {
            masm.Ldr(w2, code[2]);
            masm.Ccmp(w9, w2, NoFlag, eq);
        }
    }
    masm.B(mismatch, ne);
}

inline void EmitEeSmcExitPredicate(vixl::aarch64::MacroAssembler& masm,
    const EeSmcExitRule& rule, const vixl::aarch64::Register& source,
    const vixl::aarch64::Register& compare, vixl::aarch64::Label* accepted)
{
    using namespace vixl::aarch64;
    const Register value = rule.prefixWords == 3 ? x9 : source;
    if (rule.prefixWords == 3)
        masm.And(w9, source.W(), rule.mask);
    if (!rule.compare)
    {
        if (rule.equal)
            masm.Cbz(value, accepted);
        else
            masm.Cbnz(value, accepted);
    }
    else
    {
        masm.Cmp(value, compare);
        masm.B(accepted, rule.equal ? eq : ne);
    }
}
