// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <arm_neon.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Vif1Strided4
{
template <bool Unsigned>
inline uint32x4_t Widen(uint16x4_t values)
{
    if constexpr (Unsigned)
        return vmovl_u16(values);
    else
        return vreinterpretq_u32_s32(vmovl_s16(vreinterpret_s16_u16(values)));
}

template <bool Unsigned>
inline uint16x8_t WidenBytes(uint8x8_t values)
{
    if constexpr (Unsigned)
        return vmovl_u8(values);
    else
        return vreinterpretq_u16_s16(vmovl_s8(vreinterpret_s8_u8(values)));
}

template <bool V2_16, bool Unsigned>
inline void StoreTwo(std::uint8_t* first, std::uint8_t* second, uint8x8_t bytes)
{
    auto* a = reinterpret_cast<std::uint32_t*>(first);
    auto* b = reinterpret_cast<std::uint32_t*>(second);
    if constexpr (V2_16)
    {
        const auto values = vreinterpretq_u64_u32(Widen<Unsigned>(vreinterpret_u16_u8(bytes)));
        vst1q_u32(a, vreinterpretq_u32_u64(vdupq_laneq_u64(values, 0)));
        vst1q_u32(b, vreinterpretq_u32_u64(vdupq_laneq_u64(values, 1)));
    }
    else
    {
        const auto values = WidenBytes<Unsigned>(bytes);
        vst1q_u32(a, Widen<Unsigned>(vget_low_u16(values)));
        vst1q_u32(b, Widen<Unsigned>(vget_high_u16(values)));
    }
}

template <bool V2_16, bool Unsigned>
inline void CopyRows(std::uint8_t* dest, const std::uint8_t* source, unsigned rows, std::size_t stride)
{
    while (rows >= 4)
    {
        const auto input = vld1q_u8(source);
        StoreTwo<V2_16, Unsigned>(dest, dest + stride, vget_low_u8(input));
        StoreTwo<V2_16, Unsigned>(dest + stride * 2, dest + stride * 3, vget_high_u8(input));
        rows -= 4;
        if (!rows)
            return;
        source += 16;
        dest += stride * 4;
    }
    if (rows >= 2)
    {
        StoreTwo<V2_16, Unsigned>(dest, dest + stride, vld1_u8(source));
        rows -= 2;
        if (!rows)
            return;
        source += 8;
        dest += stride * 2;
    }
    if (rows)
    {
        std::uint32_t word;
        std::memcpy(&word, source, sizeof(word));
        const auto input = vcreate_u8(word);
        uint32x4_t result;
        if constexpr (V2_16)
        {
            const auto values = vreinterpretq_u64_u32(Widen<Unsigned>(vreinterpret_u16_u8(input)));
            result = vreinterpretq_u32_u64(vdupq_laneq_u64(values, 0));
        }
        else
        {
            result = Widen<Unsigned>(vget_low_u16(WidenBytes<Unsigned>(input)));
        }
        vst1q_u32(reinterpret_cast<std::uint32_t*>(dest), result);
    }
}
}
