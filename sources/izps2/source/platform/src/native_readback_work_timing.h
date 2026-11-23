#pragma once

#include <cstdint>

struct NativeReadbackWorkTiming
{
    struct Stage
    {
        std::uint64_t count, ticks, maximum;

        void Add(std::uint64_t begin, std::uint64_t end)
        {
            const auto duration = end - begin;
            ++count;
            ticks = duration > UINT64_MAX - ticks ? UINT64_MAX : ticks + duration;
            if (duration > maximum)
                maximum = duration;
        }
    };

    std::uint64_t hz, clock_errors, unsubmitted_skips;
    Stage scan, map, output;

    void Record(Stage& stage, std::uint64_t frequency, std::uint64_t begin, std::uint64_t end)
    {
        if (!frequency)
            return;
        if ((hz && hz != frequency) || end < begin)
        {
            ++clock_errors;
            return;
        }
        hz = frequency;
        stage.Add(begin, end);
    }
};

static_assert(sizeof(NativeReadbackWorkTiming) == 12 * sizeof(std::uint64_t));
