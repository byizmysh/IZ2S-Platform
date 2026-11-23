// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <cstdint>

constexpr bool EeDmacCausePending(std::uint32_t stat)
{
    // DMAC_STAT packs the 16-bit pending and enable halves together.
    return (stat & ((stat >> 16) | 0x8000u)) != 0;
}
