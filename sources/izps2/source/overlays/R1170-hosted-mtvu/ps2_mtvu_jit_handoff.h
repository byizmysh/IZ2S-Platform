#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

struct MtvuJitRange {
    std::size_t offset;
    std::size_t end;
};

class MtvuJitHandoff {
public:
    void Add(std::size_t offset, std::size_t end)
    {
        if (offset >= end)
            return;
        const auto first = std::lower_bound(m_ranges.begin(), m_ranges.end(), offset,
            [](MtvuJitRange range, std::size_t offset) { return range.end < offset; });
        auto last = first;
        while (last != m_ranges.end() && last->offset <= end) {
            offset = std::min(offset, last->offset);
            end = std::max(end, last->end);
            ++last;
        }
        m_ranges.insert(m_ranges.erase(first, last), {offset, end});
    }

    const std::vector<MtvuJitRange>& Ranges() const { return m_ranges; }
    void Clear() { m_ranges.clear(); }

private:
    std::vector<MtvuJitRange> m_ranges;
};
