#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// An unmasked V4-32 packet with CL == WL and MODE == 0 has no per-row
// transformation. Keep the JIT or wrapping path for all other transfers.
template <std::size_t VuBytes>
inline bool VifTryShortCopy(std::uint8_t* memory, unsigned address, const std::uint8_t* source,
    unsigned source_bytes, unsigned rows)
{
    const unsigned bytes = rows * 16;
    if (rows < 1 || rows > 3 || source_bytes != bytes)
        return false;

    const unsigned offset = address & (VuBytes - 16);
    if (offset > VuBytes - bytes)
        return false;

    auto* destination = memory + offset;
    const auto dst = reinterpret_cast<std::uintptr_t>(destination);
    const auto src = reinterpret_cast<std::uintptr_t>(source);
    if ((dst < src ? src - dst : dst - src) < bytes)
        return false;

    if (rows == 1)
        std::memcpy(destination, source, 16);
    else if (rows == 2)
        std::memcpy(destination, source, 32);
    else
        std::memcpy(destination, source, 48);
    return true;
}
