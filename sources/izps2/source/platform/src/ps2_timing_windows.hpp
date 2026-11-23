#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

// Single producer. Readers use completed windows once that producer has stopped.
template<class Counts>
class Ps2TimingWindows
{
public:
    static constexpr size_t Intervals = 120;
    static constexpr size_t Capacity = 48;

    struct Window
    {
        std::array<uint64_t, Intervals> ticks;
        uint64_t first_tick;
        uint64_t elapsed;
        uint32_t first_frame;
        Counts first;
        Counts last;

        std::array<uint64_t, 3> Percentiles() const
        {
            auto sorted = ticks;
            std::sort(sorted.begin(), sorted.end());
            return {sorted[(50 * Intervals + 99) / 100 - 1],
                    sorted[(95 * Intervals + 99) / 100 - 1],
                    sorted[(99 * Intervals + 99) / 100 - 1]};
        }

        uint64_t MaxInterval() const
        {
            return *std::max_element(ticks.begin(), ticks.end());
        }
    };

    void Start()
    {
        m_completed = 0;
        m_active = true;
        Pause();
    }

    void Stop() { m_active = false; }
    void Pause() { m_have_previous = false; m_interval = 0; }
    bool Recording() const { return m_active && m_completed < Capacity; }

    template<class Snapshot>
    void Add(uint32_t frame, uint64_t tick, Snapshot snapshot)
    {
        if (!Recording())
            return;
        auto& window = m_windows[m_completed];
        if (!m_have_previous || frame != m_previous_frame + 1 || tick < m_previous_tick)
        {
            m_interval = 0;
            m_have_previous = true;
            window.first_tick = tick;
            window.first_frame = frame;
            window.first = snapshot();
        }
        else
        {
            window.ticks[m_interval++] = tick - m_previous_tick;
            if (m_interval == Intervals)
            {
                window.elapsed = tick - window.first_tick;
                window.last = snapshot();
                ++m_completed;
                m_interval = 0;
                if (m_completed < Capacity)
                {
                    auto& next = m_windows[m_completed];
                    next.first = window.last;
                    next.first_frame = frame;
                    next.first_tick = tick;
                }
            }
        }
        m_previous_tick = tick;
        m_previous_frame = frame;
    }

    size_t Count() const { return m_completed; }
    const Window& Get(size_t index) const { return m_windows[index]; }

private:
    std::array<Window, Capacity> m_windows{};
    size_t m_interval = 0;
    size_t m_completed = 0;
    uint64_t m_previous_tick = 0;
    uint32_t m_previous_frame = 0;
    bool m_active = false;
    bool m_have_previous = false;
};
