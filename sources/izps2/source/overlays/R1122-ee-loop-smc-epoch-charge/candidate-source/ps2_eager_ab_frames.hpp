#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

class Ps2EagerAbFrames
{
public:
    static constexpr unsigned WarmIntervals = 240;
    static constexpr unsigned SampleIntervals = 600;
    enum class Event { None, FirstVblank, SampleStart, SampleEnd, Discontinuity };

    Event Add(uint32_t frame, uint64_t tick)
    {
        if (m_seen == WarmIntervals + SampleIntervals + 1 || m_discontinuous)
            return Event::None;
        if (m_seen != 0 && (frame != m_previous_frame + uint32_t{1} || tick <= m_previous_tick))
        {
            m_discontinuous = true;
            return Event::Discontinuity;
        }

        if (m_seen > WarmIntervals)
            m_ticks[m_seen - WarmIntervals - 1] = tick - m_previous_tick;
        m_previous_tick = tick;
        m_previous_frame = frame;
        switch (m_seen++)
        {
        case 0: return Event::FirstVblank;
        case WarmIntervals: return Event::SampleStart;
        case WarmIntervals + SampleIntervals: return Event::SampleEnd;
        default: return Event::None;
        }
    }

    std::array<uint64_t, 4> Quantiles() const
    {
        auto sorted = m_ticks;
        std::sort(sorted.begin(), sorted.end());
        return {sorted[(SampleIntervals * 50 + 99) / 100 - 1],
                sorted[(SampleIntervals * 95 + 99) / 100 - 1],
                sorted[(SampleIntervals * 99 + 99) / 100 - 1], sorted.back()};
    }

private:
    std::array<uint64_t, SampleIntervals> m_ticks{};
    uint64_t m_previous_tick = 0;
    uint32_t m_previous_frame = 0;
    unsigned m_seen = 0;
    bool m_discontinuous = false;
};
