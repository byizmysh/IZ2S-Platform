// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <cstdint>

namespace NfsFrameWitness
{
struct Counts
{
    uint64_t iterations = 0;
    uint64_t display_writes = 0;
    bool enabled = false;
};

enum class Point { None, Iteration, DisplayWrite };
constexpr Point Identify(uint32_t pc, uint32_t instruction)
{
    // First instruction after the main loop's framebuffer chooser returns.
    if (pc == 0x001d7eec && instruction == 0x0c074e26)
        return Point::Iteration;
    // The second framebuffer write in the complete scanout-register update.
    if (pc == 0x001d3848 && instruction == 0xfc4a0000)
        return Point::DisplayWrite;
    return Point::None;
}

void Configure(bool enabled);
Counts Read();
}
