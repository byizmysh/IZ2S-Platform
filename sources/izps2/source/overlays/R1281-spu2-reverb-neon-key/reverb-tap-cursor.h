// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include "reverb-reciprocal.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace Spu2Reverb
{
using TapAddresses = std::array<std::uint32_t, 16>;
constexpr std::uint32_t RamMask = 0xf'ffff;
constexpr std::size_t TapCount = 14;

// Cycles is a 32-bit stereo sample counter in SPU2. Each channel's
// position is Cycles >> 1, so this sentinel cannot name an actual position.
constexpr std::uint32_t NoPosition = UINT32_MAX;

class TapCursor
{
public:
    void Invalidate() { m_next = NoPosition; }

    bool CanAdvance(std::uint32_t position) const
    {
        return position == m_next && position < m_next_carry;
    }

    void Rebase(std::uint32_t position, Reciprocal& modulo, const TapAddresses& offsets)
    {
        m_next_carry = NoPosition;
        for (std::size_t i = 0; i < TapCount; i++)
        {
            m_relative[i] = modulo.Wrap(position + offsets[i]);
            // Crossing the carry changes the uint32_t dividend by 2^32.
            // Unless the period divides 2^32, incrementing the remainder
            // through that transition would yield a different address.
            const std::uint32_t carry = 0u - offsets[i];
            if (carry > position)
                m_next_carry = std::min(m_next_carry, carry);
        }
        m_next = position + 1;
    }

    template <bool Advance>
    TapAddresses Addresses(std::uint32_t start, std::uint32_t period)
    {
        TapAddresses result;
        if constexpr (Advance)
            ++m_next;
#if defined(__aarch64__)
        const uint32x4_t start4 = vdupq_n_u32(start);
        const uint32x4_t mask4 = vdupq_n_u32(RamMask);
        const uint32x4_t period4 = vdupq_n_u32(period);
        for (std::size_t i = 0; i < result.size(); i += 4)
        {
            uint32x4_t relative = vld1q_u32(m_relative.data() + i);
            if constexpr (Advance)
            {
                relative = vaddq_u32(relative, vdupq_n_u32(1));
                relative = vandq_u32(relative, vcltq_u32(relative, period4));
                vst1q_u32(m_relative.data() + i, relative);
            }
            vst1q_u32(result.data() + i, vandq_u32(vaddq_u32(relative, start4), mask4));
        }
#else
        for (std::size_t i = 0; i < result.size(); i++)
        {
            if constexpr (Advance)
            {
                const std::uint32_t next = m_relative[i] + 1;
                m_relative[i] = next < period ? next : 0;
            }
            result[i] = (m_relative[i] + start) & RamMask;
        }
#endif
        return result;
    }

private:
    alignas(16) TapAddresses m_relative{};
    std::uint32_t m_next = NoPosition;
    std::uint32_t m_next_carry = NoPosition;
};
}
