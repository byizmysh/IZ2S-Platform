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
