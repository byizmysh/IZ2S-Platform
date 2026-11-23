// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include "native_ee_warm_profile.hpp"

#include <algorithm>
#include <span>
#include <vector>

namespace ps2::ee_warm
{
class BootCoverage
{
public:
    explicit BootCoverage(std::span<const Entry> entries)
    {
        std::vector<Entry> sorted(entries.begin(), entries.end());
        std::sort(sorted.begin(), sorted.end(), [](const Entry& a, const Entry& b) { return a.pc < b.pc; });
        for (const Entry& entry : sorted)
        {
            if (m_spans.empty() || End(m_spans.back()) < entry.pc)
                m_spans.push_back(entry);
            else
                m_spans.back().bytes = std::max(End(m_spans.back()), End(entry)) - m_spans.back().pc;
        }
    }

    bool Intersects(const Entry& entry) const
    {
        const auto first = std::lower_bound(m_spans.begin(), m_spans.end(), entry.pc,
            [](const Entry& span, uint32_t pc) { return End(span) <= pc; });
        return first != m_spans.end() && first->pc < End(entry);
    }

    std::span<const Entry> Spans() const { return m_spans; }

private:
    static uint32_t End(const Entry& entry) { return entry.pc + entry.bytes; }
    std::vector<Entry> m_spans;
};
}
