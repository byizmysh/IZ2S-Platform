// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

template <std::size_t Kinds, std::size_t Peaks>
class NfsGsPeaks
{
public:
    struct Span
    {
        uint64_t begin = 0;
        uint64_t end = 0;
        uint64_t mtvu_wait = 0;
        uint32_t field = 0;
        uint32_t kind = 0;

        uint64_t Duration() const { return end - begin; }
    };

    struct Totals
    {
        uint64_t ticks = 0;
        uint64_t calls = 0;
    };

    void Start(uint64_t threshold)
    {
        m_peaks = {};
        m_totals = {};
        m_threshold = threshold;
    }

    void Add(Span span)
    {
        if (span.end < span.begin || span.kind >= Kinds)
            return;
        auto& total = m_totals[span.kind];
        ++total.calls;
        total.ticks += span.Duration();
        if (span.Duration() < m_threshold)
            return;
        auto& rows = m_peaks[span.kind];
        auto* least = &rows.front();
        for (auto& row : rows)
        {
            if (row.Duration() < least->Duration())
                least = &row;
        }
        if (span.Duration() > least->Duration())
            *least = span;
    }

    const auto& Rows() const { return m_peaks; }
    const auto& Counts() const { return m_totals; }

private:
    std::array<std::array<Span, Peaks>, Kinds> m_peaks{};
    std::array<Totals, Kinds> m_totals{};
    uint64_t m_threshold = 1;
};
