#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

struct EeCompileSpan
{
    std::uint32_t field, pc, bytes, eligible;
    std::uint64_t hash;
};
static_assert(sizeof(EeCompileSpan) == 24);

class EeCompileSpanTrace
{
public:
    void Record(std::uint32_t pc, std::uint32_t bytes, bool eligible, std::uint64_t hash)
    {
        if (m_size == m_records.size())
        {
            ++m_lost;
            return;
        }
        m_records[m_size++] = {m_field, pc, bytes, eligible, hash};
    }

    void SetField(std::uint32_t field) { m_field = field; }
    std::span<const EeCompileSpan> Records() const { return {m_records.data(), m_size}; }
    std::uint32_t Lost() const { return m_lost; }

private:
    // The known 2,160-field replay compiled 9,691 blocks. Refuse to
    // certify an incomplete trace if another run exceeds this capacity.
    std::array<EeCompileSpan, 16384> m_records;
    std::uint32_t m_size = 0, m_lost = 0, m_field = 0;
};

extern EeCompileSpanTrace* iz_ps2_ee_span_trace;

// Native warm files use a little-endian 24-byte header followed by
// fixed-width 16-byte records. Only the count may differ in the prefix.
inline bool ExtendsEeCompileSeed(std::span<const std::uint8_t> prefix,
    std::span<const std::uint8_t> candidate, std::uint32_t max_records)
{
    constexpr std::size_t header = 24, record = 16;
    if (prefix.size() < header || candidate.size() <= prefix.size() ||
        candidate.size() > header + max_records * record ||
        (candidate.size() - header) % record)
        return false;
    std::uint32_t count = 0;
    for (unsigned i = 0; i < 4; ++i)
        count |= static_cast<std::uint32_t>(candidate[16 + i]) << (8 * i);
    return count == (candidate.size() - header) / record &&
        std::equal(prefix.begin(), prefix.begin() + 16, candidate.begin()) &&
        std::equal(prefix.begin() + 20, prefix.end(), candidate.begin() + 20);
}
