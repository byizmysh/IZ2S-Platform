#pragma once

#include <cstdint>

struct EeScalarAccess
{
    std::uint32_t bytes = 0;
    bool sign = false;
    bool store = false;
};

constexpr EeScalarAccess eeDecodeScalarAccess(std::uint32_t instruction)
{
    switch (instruction >> 26)
    {
    case 0x20: return {1, true};  // LB
    case 0x21: return {2, true};  // LH
    case 0x23: return {4, true};  // LW
    case 0x24: return {1, false}; // LBU
    case 0x25: return {2, false}; // LHU
    case 0x27: return {4, false}; // LWU
    case 0x37: return {8, false}; // LD
    case 0x28: return {1, false, true}; // SB
    case 0x29: return {2, false, true}; // SH
    case 0x2b: return {4, false, true}; // SW
    case 0x3f: return {8, false, true}; // SD
    case 0x1e: return {16, false}; // LQ
    case 0x1f: return {16, false, true}; // SQ
    default: return {};
    }
}

// Each access contributes an alignment constraint on one common runtime base.
// If the residues agree, only the strongest constraint needs a runtime test.
constexpr unsigned eeGroupZeroDestination(std::uint32_t op)
{
    if (op == 0)
        return 0;
    if ((op & 0xffff07c0u) == 0)
    {
        switch (op & 63)
        {
        case 0x21: // ADDU rd, zero, zero
        case 0x25: // OR rd, zero, zero
        case 0x2d: // DADDU rd, zero, zero
            return (op >> 11) & 31;
        }
    }
    return 32;
}

constexpr bool eeCanGroupAddressIndependentOp(std::uint32_t op, std::uint32_t base)
{
    const unsigned zero = eeGroupZeroDestination(op);
    if (zero < 32)
        return zero == 0 || zero != base;
    if (((op >> 16) & 31) == base)
        return false;
    switch (op >> 26)
    {
    case 0x09: // ADDIU does not trap on overflow.
    case 0x0c: // ANDI
    case 0x0d: // ORI
    case 0x0e: // XORI
        return true;
    default:
        return false;
    }
}

constexpr bool eeIsAdjacentLoadPair(std::uint32_t first, std::uint32_t second)
{
    const auto access = eeDecodeScalarAccess(first);
    if (access.store || (access.bytes != 4 && access.bytes != 8) ||
        ((first ^ second) >> 21) != 0)
        return false;

    const unsigned base = (first >> 21) & 31;
    const unsigned firstDest = (first >> 16) & 31;
    const unsigned secondDest = (second >> 16) & 31;
    return firstDest && secondDest && firstDest != secondDest && firstDest != base && secondDest != base &&
        static_cast<std::int32_t>(static_cast<std::int16_t>(second)) ==
            static_cast<std::int16_t>(first) + static_cast<std::int32_t>(access.bytes);
}

constexpr bool eeIsLoadPairAcrossZero(std::uint32_t first, std::uint32_t middle, std::uint32_t last)
{
    const unsigned zero = eeGroupZeroDestination(middle);
    return zero < 32 && zero != ((first >> 16) & 31) && zero != ((last >> 16) & 31) &&
        (zero == 0 || zero != ((first >> 21) & 31)) && eeIsAdjacentLoadPair(first, last);
}

constexpr bool eeIsUnalignedDwordHalf(unsigned opcode)
{
    return opcode == 0x1a || opcode == 0x1b || opcode == 0x2c || opcode == 0x2d;
}

struct EeUnalignedDwordPair
{
    bool valid = false;
    bool store = false;
    std::int32_t offset = 0;
};

constexpr EeUnalignedDwordPair eeDecodeUnalignedDwordPair(std::uint32_t first, std::uint32_t second)
{
    const unsigned op = first >> 26;
    if (!eeIsUnalignedDwordHalf(op) || (second >> 26) != (op ^ 1) ||
        ((first ^ second) & 0x03ff0000u))
        return {};
    const unsigned base = (first >> 21) & 31;
    const unsigned target = (first >> 16) & 31;
    const bool store = op == 0x2c || op == 0x2d;
    // The second load must still address the original base register.
    if (!store && (!target || target == base))
        return {};
    const std::int32_t firstOffset = static_cast<std::int16_t>(first);
    const std::int32_t secondOffset = static_cast<std::int16_t>(second);
    const std::int32_t right = (op & 1) ? firstOffset : secondOffset;
    const std::int32_t left = (op & 1) ? secondOffset : firstOffset;
    if (left - right != 7)
        return {};
    return {true, store, right};
}

struct EeMemoryPageSpan
{
    std::int32_t first = 0;
    std::int32_t end = 0;
    std::int32_t anchor = 0;
    std::uint32_t alignment = 0;

    constexpr bool extend(std::int32_t offset, std::uint32_t bytes, std::int32_t limit)
    {
        const auto shared = alignment < bytes ? alignment : bytes;
        if (shared && (static_cast<std::uint32_t>(anchor - offset) & (shared - 1)))
            return false;
        const auto last = offset + static_cast<std::int32_t>(bytes);
        const auto low = !alignment || offset < first ? offset : first;
        const auto high = !alignment || last > end ? last : end;
        if (high - low > limit)
            return false;

        first = low;
        end = high;
        if (bytes > alignment)
        {
            alignment = bytes;
            anchor = offset;
        }
        return true;
    }
};
