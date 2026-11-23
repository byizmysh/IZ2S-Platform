// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <arm_neon.h>
#include <cstddef>
#include <cstdint>

namespace Vif1Strided16
{
template <bool Unsigned>
inline void WidenPair(std::uint8_t* first, std::uint8_t* second, uint16x8_t input)
{
    auto* out_first = reinterpret_cast<std::uint32_t*>(first);
    auto* out_second = reinterpret_cast<std::uint32_t*>(second);
    if constexpr (Unsigned)
    {
        vst1q_u32(out_first, vmovl_u16(vget_low_u16(input)));
        vst1q_u32(out_second, vmovl_high_u16(input));
    }
    else
    {
        const auto values = vreinterpretq_s16_u16(input);
        vst1q_u32(out_first, vreinterpretq_u32_s32(vmovl_s16(vget_low_s16(values))));
        vst1q_u32(out_second, vreinterpretq_u32_s32(vmovl_high_s16(values)));
    }
}

template <bool Unsigned>
inline void CopyRows(std::uint8_t* dest, const std::uint8_t* source, unsigned rows, std::size_t stride)
{
    unsigned i = 0;
    for (; rows - i >= 4; i += 4)
    {
        const auto a = vreinterpretq_u16_u8(vld1q_u8(source + i * 8));
        const auto b = vreinterpretq_u16_u8(vld1q_u8(source + i * 8 + 16));
        auto* out = dest + i * stride;
        WidenPair<Unsigned>(out, out + stride, a);
        WidenPair<Unsigned>(out + stride * 2, out + stride * 3, b);
    }
    if (rows - i >= 2)
    {
        const auto packed = vreinterpretq_u16_u8(vld1q_u8(source + i * 8));
        auto* out = dest + i * stride;
        WidenPair<Unsigned>(out, out + stride, packed);
        i += 2;
    }
    if (i < rows)
    {
        const auto packed = vreinterpret_u16_u8(vld1_u8(source + i * 8));
        auto* out = reinterpret_cast<std::uint32_t*>(dest + i * stride);
        if constexpr (Unsigned)
            vst1q_u32(out, vmovl_u16(packed));
        else
            vst1q_u32(out, vreinterpretq_u32_s32(vmovl_s16(vreinterpret_s16_u16(packed))));
    }
}
}
