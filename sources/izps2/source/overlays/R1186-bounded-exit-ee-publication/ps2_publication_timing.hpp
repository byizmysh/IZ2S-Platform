#pragma once

#include <cstdint>

struct Ps2PublicationTiming {
    std::uint64_t primary_wait_ticks;
    std::uint64_t vu1_calls;
    std::uint64_t vu1_ticks;
    std::uint64_t vu1_wait_ticks;
};

Ps2PublicationTiming ps2_publication_timing();
