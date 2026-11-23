#pragma once

#include <cstdint>

struct Spu2MixTiming
{
    enum Stage { Input, Voices, CoreMix, Output, Finish, StageCount };
    std::uint64_t ticks[StageCount]{};
    std::uint64_t calls = 0;
    std::uint64_t samples = 0;
    std::uint64_t tick_hz = 0;
};

// Sampling and snapshots run on the synchronous IOP owner.
void iz_ps2_set_spu2_mix_timing(bool enabled);
const Spu2MixTiming& iz_ps2_get_spu2_mix_timing();
