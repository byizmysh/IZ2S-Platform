#pragma once

#include <cstdint>

namespace Spu2Reverb
{
// Exact remainder for all 32-bit dividends. The low half of the
// reciprocal product yields the fractional quotient. Scaling it by the
// period in a full-width multiply recovers the remainder.
class Reciprocal
{
public:
    void SetPeriod(std::uint32_t period)
    {
        if (period == m_period)
            return;
        m_period = period;
        m_multiplier = period > 1 ? UINT64_MAX / period + 1 : 0;
    }

    std::uint32_t Wrap(std::uint32_t value) const
    {
        const std::uint64_t fraction = m_multiplier * value;
        return static_cast<std::uint32_t>((static_cast<__uint128_t>(fraction) * m_period) >> 64);
    }

private:
    std::uint32_t m_period = 0;
    std::uint64_t m_multiplier = 0;
};
}
