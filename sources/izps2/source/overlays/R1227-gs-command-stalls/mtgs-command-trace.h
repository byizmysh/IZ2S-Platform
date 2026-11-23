#pragma once

#include <array>
#include <atomic>
#include <cstdint>

// One GS writer and one EE reader. A published row stays immutable until
// the next VM run, after the old GS queue has closed.
class MtgsCommandTrace
{
public:
    static constexpr unsigned Capacity = 48;

    struct Sample
    {
        uint64_t begin;
        uint64_t end;
        uint32_t command;
        unsigned ring_position;
        unsigned pending_quadwords;
    };

    void Start(uint64_t slow_ticks)
    {
        m_threshold = slow_ticks;
        m_size.store(0, std::memory_order_relaxed);
        m_active.store(true, std::memory_order_release);
    }

    void Stop() { m_active.store(false, std::memory_order_relaxed); }
    bool Active() const { return m_active.load(std::memory_order_acquire); }

    void Record(const Sample& sample)
    {
        if (sample.end < sample.begin || sample.end - sample.begin < m_threshold)
            return;
        const unsigned slot = m_size.load(std::memory_order_relaxed);
        if (slot == Capacity)
            return;
        m_rows[slot] = sample;
        m_size.store(slot + 1, std::memory_order_release);
    }

    unsigned Size() const { return m_size.load(std::memory_order_acquire); }
    const Sample& Get(unsigned index) const { return m_rows[index]; }

private:
    std::array<Sample, Capacity> m_rows{};
    uint64_t m_threshold = 0;
    std::atomic<unsigned> m_size{0};
    std::atomic<bool> m_active{false};
};
