#pragma once

#include "arm64/EeSoftmemPages.h"

inline constexpr unsigned EeWriteGuardTagBit = 1;
inline constexpr unsigned EeWriteCodeMaskBit = 2;
inline constexpr unsigned EeWriteChunkShift = 7;
inline constexpr unsigned EeWriteChunkBytes = 1u << EeWriteChunkShift;
inline constexpr unsigned EeWriteChunkIndexBits = 5;

constexpr uint32_t EeWriteChunkMask(uint32_t begin, uint32_t end)
{
    return (UINT32_MAX << (begin >> EeWriteChunkShift)) &
        (UINT32_MAX >> (31u - ((end - 1u) >> EeWriteChunkShift)));
}

constexpr uintptr_t EeEncodeWriteGuard(uint32_t codeMask)
{
    return EeSoftmemIndirectPage | (uintptr_t{1} << EeWriteGuardTagBit) |
        (uintptr_t{codeMask} << EeWriteCodeMaskBit);
}
