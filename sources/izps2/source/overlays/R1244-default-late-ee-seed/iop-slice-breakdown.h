#pragma once

#include <cstdint>

struct IopSliceBreakdown
{
    std::uint64_t phase_ticks[4]{};
    std::uint64_t jit_samples = 0;
    std::uint64_t idle_samples = 0;
    std::uint64_t quiet_event_samples = 0;
    std::uint64_t pending_event_samples = 0;
    std::uint64_t samples = 0;
    std::uint64_t tick_hz = 0;
};

void iz_ps2_configure_iop_slice_breakdown(bool enabled);
const IopSliceBreakdown& iz_ps2_get_iop_slice_breakdown();
