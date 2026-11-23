#pragma once

#include <algorithm>
#include <cstdint>

// NUM=0 encodes 256 rows. A skip advances only between emitted row groups.
inline std::uint16_t VifUnpackSpan(unsigned cl, unsigned wl, std::uint8_t num)
{
    const unsigned rows = num ? num : 256;
    const unsigned write_length = wl ? wl : 256;
    const unsigned skipped = cl > write_length
        ? ((rows - 1) / write_length) * (cl - write_length)
        : 0;
    return static_cast<std::uint16_t>(std::min(16 * (rows + skipped), 0xffffu));
}
