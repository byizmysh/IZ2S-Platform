#pragma once

#include <stddef.h>

enum { PS2_EE_WARM_MAX_RANGES = 32, PS2_EE_WARM_MAX_BYTES = 32768 };
typedef struct {
    size_t offset;
    size_t size;
} Ps2EeWarmRange;

#ifdef __cplusplus
#include <algorithm>
#include <array>
#include <limits>
#include <span>

class Ps2EeWarmRanges
{
public:
    bool Add(size_t offset, size_t size, size_t budget = PS2_EE_WARM_MAX_BYTES)
    {
        if (!size || size > std::numeric_limits<size_t>::max() - offset || m_bytes > budget)
            return false;

#ifdef IZPS2_SWITCH2_EE_PUBLICATION_UNION
        // The provider reads the final bytes from one staging view. Sorting
        // and uniting overlapping writes cannot change the published image.
        size_t first = m_count;
        while (first && m_ranges[first - 1].offset + m_ranges[first - 1].size >= offset)
            --first;

        size_t end = offset + size;
        size_t covered = 0;
        size_t last = first;
        while (last < m_count && m_ranges[last].offset <= end)
        {
            const auto& range = m_ranges[last++];
            offset = std::min(offset, range.offset);
            end = std::max(end, range.offset + range.size);
            covered += range.size;
        }

        const size_t new_bytes = end - offset - covered;
        if (new_bytes > budget - m_bytes ||
            (first == last && m_count == m_ranges.size()))
            return false;

        const auto begin = m_ranges.begin();
        if (first == last)
            std::copy_backward(begin + first, begin + m_count, begin + m_count + 1);
        else if (last > first + 1)
            std::copy(begin + last, begin + m_count, begin + first + 1);

        m_ranges[first] = {offset, end - offset};
        m_count = m_count - (last - first) + 1;
        m_bytes += new_bytes;
#else
        if (m_count)
        {
            auto& last = m_ranges[m_count - 1];
            const size_t end = offset + size;
            const size_t last_end = last.offset + last.size;
            if (offset <= last_end && end >= last.offset)
            {
                const size_t start = std::min(offset, last.offset);
                const size_t combined = std::max(end, last_end) - start;
                if (combined - last.size > budget - m_bytes)
                    return false;
                m_bytes += combined - last.size;
                last = {start, combined};
                return true;
            }
        }

        if (m_count == m_ranges.size() || size > budget - m_bytes)
            return false;
        m_ranges[m_count++] = {offset, size};
        m_bytes += size;
#endif
        return true;
    }

    std::span<const Ps2EeWarmRange> Pending() const
    {
        return std::span(m_ranges).first(m_count);
    }

    void Reset()
    {
        m_count = 0;
        m_bytes = 0;
    }

private:
    std::array<Ps2EeWarmRange, PS2_EE_WARM_MAX_RANGES> m_ranges{};
    size_t m_count = 0;
    size_t m_bytes = 0;
};
#endif
