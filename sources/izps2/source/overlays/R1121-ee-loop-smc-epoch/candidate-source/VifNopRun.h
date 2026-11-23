#pragma once

#include <cstdint>

// The first word is a NOP without the I bit. Keep the final NOP on the
// normal path when its lookahead can cause an MSKPATH3 timing break.
inline std::uint32_t VifNopRunLength(const std::uint32_t* words, std::uint32_t count)
{
    std::uint32_t n = 1;
    while (n < count && (words[n] >> 24) == 0)
        ++n;
    if (n < count && (words[n] & 0x7f000001u) == 0x06000001u)
        --n;
    return n;
}
