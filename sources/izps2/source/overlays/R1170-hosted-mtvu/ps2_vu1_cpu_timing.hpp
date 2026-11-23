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
