#pragma once

#include <cstdint>

// The GS worker writes this bank. The frontend reads it after worker join.
struct NativeReadbackTiming
{
    struct Window
    {
        std::uint64_t intervals, total_ticks, longest_ticks, gaps_50ms, gaps_100ms;
    };

    std::uint64_t scans, skipped_scans, tick_hz, first_tick, last_tick, regressions, have_previous;
    Window windows[3];

    void Accept(std::uint64_t now, std::uint64_t hz)
    {
        if (!tick_hz)
        {
            tick_hz = hz;
            first_tick = now;
        }
        if (now < first_tick || (have_previous && now < last_tick))
        {
            ++regressions;
            have_previous = 0;
            return;
        }
        if (have_previous)
        {
            const auto age = now - first_tick;
            const auto gap = now - last_tick;
            auto& window = windows[age < 60 * hz ? 0 : age < 90 * hz ? 1 : 2];
            ++window.intervals;
            window.total_ticks += gap;
            if (gap > window.longest_ticks)
                window.longest_ticks = gap;
            if (gap >= (hz + 19) / 20)
                ++window.gaps_50ms;
            if (gap >= (hz + 9) / 10)
                ++window.gaps_100ms;
        }
        last_tick = now;
        have_previous = 1;
    }
};
static_assert(sizeof(NativeReadbackTiming) == 22 * sizeof(std::uint64_t));
