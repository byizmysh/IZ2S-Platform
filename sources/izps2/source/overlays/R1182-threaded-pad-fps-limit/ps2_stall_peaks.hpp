#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

// Retain the longest measured intervals without logging in the EE hot path.
template<class Timing, std::size_t Capacity>
class Ps2StallPeaks {
public:
    struct Interval {
        std::uint64_t ticks = 0;
        std::uint32_t frame = 0;
        Timing begin{};
        Timing end{};
    };

    void Start(bool enabled, std::uint64_t minimum_ticks)
    {
        m_enabled = enabled;
        m_minimum_ticks = minimum_ticks;
        m_count = 0;
        m_qualified = 0;
        Pause();
    }

    void Pause() { m_have_previous = false; }
    bool Recording() const { return m_enabled; }
    std::size_t Count() const { return m_count; }
    std::uint64_t Qualified() const { return m_qualified; }
    const Interval& Get(std::size_t i) const { return m_longest[i]; }

    void Add(std::uint32_t frame, std::uint64_t tick, const Timing& timing)
    {
        if (!Recording())
            return;
        if (m_have_previous && frame == m_frame + 1 && tick >= m_tick)
        {
            const std::uint64_t span = tick - m_tick;
            if (span >= m_minimum_ticks)
            {
                ++m_qualified;
                Record(span, timing);
            }
        }
        m_previous = timing;
        m_frame = frame;
        m_tick = tick;
        m_have_previous = true;
    }

private:
    void Record(std::uint64_t span, const Timing& end)
    {
        if (m_count < Capacity)
        {
            m_longest[m_count++] = {span, m_frame, m_previous, end};
            return;
        }
        const auto shortest = std::min_element(m_longest.begin(), m_longest.end(),
            [](const Interval& lhs, const Interval& rhs) { return lhs.ticks < rhs.ticks; });
        if (span > shortest->ticks)
            *shortest = {span, m_frame, m_previous, end};
    }

    static_assert(Capacity > 0);
    std::array<Interval, Capacity> m_longest{};
    std::size_t m_count = 0;
    std::uint64_t m_qualified = 0, m_minimum_ticks = 0, m_tick = 0;
    std::uint32_t m_frame = 0;
    bool m_enabled = false, m_have_previous = false;
    Timing m_previous{};
};
