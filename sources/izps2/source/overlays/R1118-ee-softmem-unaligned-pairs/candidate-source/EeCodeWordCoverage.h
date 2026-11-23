#pragma once

#include <array>
#include <cstdint>

// One bit per 32-bit guest word that belongs to a compiled code span.
// Deletion may leave bits set until the whole EE cache is reset.
template<uint32_t RamBytes>
class EeCodeWordCoverage
{
public:
    void reset() { m_words.fill(0); }

    // Both methods take nonempty half-open intervals inside canonical RAM.
    void mark(uint32_t begin, uint32_t end)
    {
        const uint32_t first = begin / BytesPerBucket;
        const uint32_t last = (end - 1) / BytesPerBucket;
        const uint64_t lower = firstMask(begin);
        const uint64_t upper = lastMask(end);
        if (first == last)
        {
            m_words[first] |= lower & upper;
            return;
        }
        m_words[first] |= lower;
        for (uint32_t i = first + 1; i < last; ++i)
            m_words[i] = UINT64_MAX;
        m_words[last] |= upper;
    }

    bool overlaps(uint32_t begin, uint32_t end) const
    {
        const uint32_t first = begin / BytesPerBucket;
        const uint32_t last = (end - 1) / BytesPerBucket;
        const uint64_t lower = firstMask(begin);
        const uint64_t upper = lastMask(end);
        if (first == last)
            return (m_words[first] & lower & upper) != 0;
        if ((m_words[first] & lower) || (m_words[last] & upper))
            return true;
        for (uint32_t i = first + 1; i < last; ++i)
        {
            if (m_words[i])
                return true;
        }
        return false;
    }

private:
    static constexpr uint32_t BytesPerBucket = 64 * 4;
    static_assert(RamBytes % BytesPerBucket == 0);

    static uint64_t firstMask(uint32_t begin)
    {
        return UINT64_MAX << ((begin % BytesPerBucket) / 4);
    }

    static uint64_t lastMask(uint32_t end)
    {
        return UINT64_MAX >> (63 - ((end - 1) % BytesPerBucket) / 4);
    }

    std::array<uint64_t, RamBytes / BytesPerBucket> m_words{};
};
