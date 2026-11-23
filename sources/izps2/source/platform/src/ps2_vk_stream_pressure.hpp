#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

class Ps2VkStreamPressure
{
public:
    enum Event
    {
        FenceWait,
        FenceTicks,
        ReserveMiss,
        Grew,
        GrowFailed,
        EventCount,
    };

    static constexpr std::array Names{"vertex", "index", "expand-index",
        "vs-uniform", "fs-uniform", "texture", "other"};
    using Counts = std::array<uint64_t, EventCount>;
    using Totals = std::array<Counts, Names.size()>;

    void Record(const char* stream, Event event)
    {
        m_totals[FindStream(stream)][event].fetch_add(1, std::memory_order_relaxed);
    }

    void RecordFenceWait(const char* stream, uint64_t elapsed)
    {
        auto& totals = m_totals[FindStream(stream)];
        totals[FenceTicks].fetch_add(elapsed, std::memory_order_relaxed);
        totals[FenceWait].fetch_add(1, std::memory_order_relaxed);
    }

    // Fields are monotonic but independently sampled across GS and EE threads.
    Totals Read() const
    {
        Totals snapshot{};
        for (size_t stream = 0; stream < Names.size(); ++stream)
            for (size_t event = 0; event < EventCount; ++event)
                snapshot[stream][event] = m_totals[stream][event].load(std::memory_order_relaxed);
        return snapshot;
    }

private:
    static size_t FindStream(const char* stream)
    {
        if (stream)
        {
            const std::string_view name(stream);
            for (size_t i = 0; i < Names.size() - 1; ++i)
                if (name == Names[i])
                    return i;
        }
        return Names.size() - 1;
    }

    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    std::array<std::array<std::atomic<uint64_t>, EventCount>, Names.size()> m_totals{};
};

extern Ps2VkStreamPressure g_ps2_vk_stream_pressure;
