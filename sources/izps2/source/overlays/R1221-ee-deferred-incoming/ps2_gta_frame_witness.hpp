#pragma once

#include <cstdint>

struct Ps2GtaFrameWitness
{
    uint64_t commits;
    bool enabled;
};

void iz_ps2_set_gta_frame_witness(bool enabled);
Ps2GtaFrameWitness iz_ps2_gta_frame_witness();
