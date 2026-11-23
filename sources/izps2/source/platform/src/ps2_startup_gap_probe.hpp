#pragma once

#include <cstddef>
#include <cstdint>

// The EE owner records one startup window and publishes the peak after Execute returns.
template<class Timing, size_t Intervals>
class Ps2StartupGapProbe
{
public:
    struct Peak
    {
        uint64_t ticks = 0;
        uint32_t start_frame = 0;
        Timing first{};
        Timing last{};
    };

    void Start(bool enabled)
    {
        m_enabled = enabled;
        m_intervals = 0;
        m_have_previous = false;
        m_peak = {};
    }

    void Pause()
    {
        if (Recording())
        {
            m_intervals = 0;
            m_have_previous = false;
            m_peak = {};
        }
    }

    bool Recording() const { return m_enabled && m_intervals < Intervals; }

    template<class Snapshot>
    void Add(uint32_t frame, uint64_t tick, Snapshot snapshot)
    {
        if (!Recording())
            return;
        if (m_have_previous && (frame != m_previous_frame + 1 || tick < m_previous_tick))
            Pause();
        const Timing now = snapshot();
        if (m_have_previous)
        {
            const uint64_t elapsed = tick - m_previous_tick;
            if (!m_intervals || elapsed > m_peak.ticks)
                m_peak = {elapsed, m_previous_frame, m_previous, now};
            ++m_intervals;
        }
        m_previous = now;
        m_previous_frame = frame;
        m_previous_tick = tick;
        m_have_previous = true;
    }

    const Peak* CompletedPeak() const { return m_intervals == Intervals ? &m_peak : nullptr; }

private:
    Timing m_previous{};
    Peak m_peak{};
    size_t m_intervals = 0;
    uint64_t m_previous_tick = 0;
    uint32_t m_previous_frame = 0;
    bool m_enabled = false;
    bool m_have_previous = false;
};
