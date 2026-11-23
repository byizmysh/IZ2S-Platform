#pragma once

#include <cstdint>

struct Ps2Vu1Sample {
    std::uint64_t calls;
    std::uint64_t selected;
    std::uint64_t measured;
    std::uint64_t cpu_ticks;
    std::uint64_t next_sample;
};

struct Ps2Vu1Timing {
    Ps2Vu1Sample execute;
    Ps2Vu1Sample xgkick;
};

Ps2Vu1Timing ps2_vu1_cpu_timing();
void ps2_reset_vu1_cpu_timing();

struct Ps2Vu1WallTiming {
    std::uint64_t execute_ticks;
    std::uint64_t jit_episode_ticks;
    std::uint64_t jit_episodes;
};

void ps2_configure_vu1_wall_timing(bool enabled);
Ps2Vu1WallTiming ps2_vu1_wall_timing();
