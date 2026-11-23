#pragma once

#include "ps2_timing_windows.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

class Ps2VideoSampleHistory
{
public:
    static constexpr std::size_t SlotCount = 8;
    static constexpr std::size_t Pixels = 64;
    using Samples = std::array<std::uint32_t, Pixels>;

    void StartChangeWindows() { change_windows.Start(); }

    // Called before rendering submission. A full ring simply leaves the
    // sample missing, never asking the GPU to wait.
    int Record(std::uint64_t serial, std::uint64_t fence)
    {
        if (pending == SlotCount || !serial || !fence)
            return -1;
        const auto index = (head + pending++) % SlotCount;
        slots[index] = {serial, fence};
        return static_cast<int>(index);
    }

    void Accept(std::uint64_t serial, std::uint64_t tick)
    {
        ++accepted;
        for (std::size_t i = 0; i < pending; ++i)
        {
            auto& slot = slots[(head + i) % SlotCount];
            if (slot.serial == serial)
            {
                slot.accepted = accepted;
                slot.tick = tick;
                return;
            }
        }
    }

    template<typename Read>
    void Drain(std::uint64_t completed, const Read& read)
    {
        while (pending && slots[head].fence <= completed)
        {
            const auto& slot = slots[head];
            if (slot.accepted)
            {
                ++sampled;
                Samples pixels;
                read(head, pixels);
                if (previous_accepted && previous_accepted + 1 == slot.accepted && slot.tick >= previous_tick)
                {
                    ++compared;
                    const auto interval = slot.tick - previous_tick;
                    compared_ticks += interval;
                    if (pixels == previous)
                        ++same;
                    else
                    {
                        ++changed;
                        change_windows.Add(static_cast<std::uint32_t>(changed), slot.tick,
                            [&slot] { return slot.accepted; });
                    }
                }
                else
                    change_windows.Pause();
                previous = pixels;
                previous_accepted = slot.accepted;
                previous_tick = slot.tick;
            }
            head = (head + 1) % SlotCount;
            --pending;
        }
    }

    std::uint64_t accepted = 0;
    std::uint64_t sampled = 0;
    std::uint64_t compared = 0;
    std::uint64_t same = 0;
    std::uint64_t changed = 0;
    std::uint64_t compared_ticks = 0;

    const auto& GetChangeWindows() const { return change_windows; }

private:
    struct Slot
    {
        std::uint64_t serial;
        std::uint64_t fence;
        std::uint64_t accepted = 0;
        std::uint64_t tick = 0;
    };
    std::array<Slot, SlotCount> slots{};
    Samples previous{};
    std::size_t head = 0;
    std::size_t pending = 0;
    std::uint64_t previous_accepted = 0;
    std::uint64_t previous_tick = 0;
    Ps2TimingWindows<std::uint64_t> change_windows;
};
