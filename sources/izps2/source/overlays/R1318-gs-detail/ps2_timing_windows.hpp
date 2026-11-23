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
    static constexpr size_t KeepStartup = 12;

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

    void Start(bool keep_latest = false)
    {
        m_completed = 0;
        m_next_latest = KeepStartup;
        m_keep_latest = keep_latest;
        m_active = true;
        Pause();
    }

    void Stop() { m_active = false; }
    void Pause() { m_have_previous = false; m_interval = 0; }
    bool Recording() const { return m_active && (m_completed < Capacity || m_keep_latest); }

    template<class Snapshot>
    void Add(uint32_t frame, uint64_t tick, Snapshot snapshot)
    {
        if (!Recording())
            return;
        auto& window = m_current;
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
                if (m_completed < Capacity)
                    m_windows[m_completed++] = window;
                else
                {
                    m_windows[m_next_latest++] = window;
                    if (m_next_latest == Capacity)
                        m_next_latest = KeepStartup;
                }
                m_interval = 0;
                window.first = window.last;
                window.first_frame = frame;
                window.first_tick = tick;
            }
        }
        m_previous_tick = tick;
        m_previous_frame = frame;
    }

    size_t Count() const { return m_completed; }
    const Window& Get(size_t index) const
    {
        if (m_keep_latest && m_completed == Capacity && index >= KeepStartup)
            index = KeepStartup + (index + m_next_latest - 2 * KeepStartup) % (Capacity - KeepStartup);
        return m_windows[index];
    }

private:
    std::array<Window, Capacity> m_windows{};
    Window m_current{};
    size_t m_interval = 0;
    size_t m_completed = 0;
    size_t m_next_latest = KeepStartup;
    uint64_t m_previous_tick = 0;
    uint32_t m_previous_frame = 0;
    bool m_active = false;
    bool m_keep_latest = false;
    bool m_have_previous = false;
};
