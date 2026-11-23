#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

template <unsigned Frames, typename Process>
class StereoDspBlockQueue
{
public:
    static constexpr std::uint32_t Capacity = 16;

    bool try_push(const float* samples, Process process)
    {
        const auto head = m_head.load(std::memory_order_relaxed);
        if (head - m_tail.load(std::memory_order_acquire) == Capacity)
            return false;

        auto& block = m_blocks[head & (Capacity - 1)];
        std::memcpy(block.samples.data(), samples, sizeof(block.samples));
        block.process = process;
        m_head.store(head + 1, std::memory_order_release);
        return true;
    }

    bool empty() const
    {
        return m_tail.load(std::memory_order_relaxed) == m_head.load(std::memory_order_acquire);
    }

    // Capture a finite batch so concurrent writes cannot starve audio output.
    unsigned drain(auto&& process, auto&& first_slot_freed)
    {
        auto tail = m_tail.load(std::memory_order_relaxed);
        const auto head = m_head.load(std::memory_order_acquire);
        const auto count = head - tail;
        if (!count)
            return 0;

        const auto consume = [&] {
            const auto& block = m_blocks[tail & (Capacity - 1)];
            process(block.samples.data(), block.process);
            m_tail.store(++tail, std::memory_order_release);
        };
        consume();
        first_slot_freed();
        while (tail != head)
            consume();
        return count;
    }

private:
    struct Block
    {
        std::array<float, Frames * 2> samples;
        Process process;
    };

    static_assert((Capacity & (Capacity - 1)) == 0);
    alignas(64) std::atomic<std::uint32_t> m_head{0};
    alignas(64) std::atomic<std::uint32_t> m_tail{0};
    alignas(64) std::array<Block, Capacity> m_blocks;
};
