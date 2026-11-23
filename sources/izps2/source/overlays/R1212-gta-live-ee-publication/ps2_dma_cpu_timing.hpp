#pragma once

#include <cstdint>

struct Ps2DmaCpuTiming
{
    enum Scope : unsigned { Scan, Vif1, Gif, Other, Count };
    std::uint64_t calls;
    std::uint64_t samples;
    std::uint64_t ticks[Count];
};

void iz_ps2_set_dma_cpu_timing(bool enabled);
Ps2DmaCpuTiming iz_ps2_dma_cpu_timing();
