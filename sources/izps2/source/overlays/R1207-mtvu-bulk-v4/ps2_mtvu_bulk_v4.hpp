#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace MtvuBulkV4
{
struct Counters
{
    bool enabled;
    std::uint64_t total;
    std::uint64_t packets[3];
    std::uint64_t rows[3];
};
constexpr unsigned MemoryBytes = 0x4000;

struct Packet
{
    unsigned command;
    unsigned mode;
    unsigned write_cycle;
    unsigned length_cycle;
    unsigned position_in_cycle;
    unsigned rows;
    unsigned address;
    unsigned bytes;
    bool unsigned_values;
};

struct Plan
{
    unsigned format;
    unsigned rows;
    unsigned offset;
    unsigned run_rows;
    unsigned gap_rows;
    unsigned address_advance;
    unsigned end_cycle;
    bool unsigned_values;
};

constexpr bool Decode(const Packet& packet, Plan& plan)
{
    const unsigned format = (packet.command & 0x1f) - 0x0c;
    if (format > 2 || packet.mode != 0 || packet.position_in_cycle != 0 ||
        packet.write_cycle == 0 || packet.length_cycle < packet.write_cycle ||
        packet.rows == 0 || packet.rows > 256 || (packet.address & 15) != 0)
        return false;
    const unsigned source_row = 16 >> format;
    if (packet.bytes != packet.rows * source_row)
        return false;

    const unsigned offset = packet.address & (MemoryBytes - 16);
    const unsigned gap = packet.length_cycle - packet.write_cycle;
    const unsigned groups = packet.rows / packet.write_cycle;
    const unsigned tail = packet.rows - groups * packet.write_cycle;
    const unsigned span_rows = packet.rows + (groups - (tail == 0)) * gap;
    if (span_rows > (MemoryBytes - offset) / 16)
        return false;

    plan = {format, packet.rows, offset, packet.write_cycle, gap,
        16 * (packet.rows + groups * gap), tail, packet.unsigned_values};
    return true;
}

#if defined(__aarch64__)
template <unsigned Format, bool Unsigned>
inline void StoreRows(std::uint8_t* destination, const std::uint8_t* source, unsigned rows)
{
    if constexpr (Format == 0)
    {
        std::memcpy(destination, source, rows * 16);
    }
    else
    {
        auto* out = reinterpret_cast<std::uint32_t*>(destination);
        for (unsigned i = 0; i < rows; ++i)
        {
            uint16x4_t lanes;
            if constexpr (Format == 1)
                std::memcpy(&lanes, source, 8);
            else
            {
                std::uint32_t packed;
                std::memcpy(&packed, source, 4);
                const uint8x8_t bytes = vreinterpret_u8_u32(vdup_n_u32(packed));
                if constexpr (Unsigned)
                    lanes = vget_low_u16(vmovl_u8(bytes));
                else
                    lanes = vreinterpret_u16_s16(vget_low_s16(vmovl_s8(vreinterpret_s8_u8(bytes))));
            }
            if constexpr (Unsigned)
                vst1q_u32(out, vmovl_u16(lanes));
            else
                vst1q_u32(out, vreinterpretq_u32_s32(vmovl_s16(vreinterpret_s16_u16(lanes))));
            out += 4;
            source += 16 >> Format;
        }
    }
}

template <unsigned Format, bool Unsigned>
inline void ExecuteRuns(const Plan& plan, std::uint8_t* memory, const std::uint8_t* source)
{
    unsigned rows = plan.rows;
    auto* destination = memory + plan.offset;
    if (plan.gap_rows == 0)
    {
        StoreRows<Format, Unsigned>(destination, source, rows);
        return;
    }

    while (rows > plan.run_rows)
    {
        StoreRows<Format, Unsigned>(destination, source, plan.run_rows);
        source += plan.run_rows * (16 >> Format);
        destination += (plan.run_rows + plan.gap_rows) * 16;
        rows -= plan.run_rows;
    }
    StoreRows<Format, Unsigned>(destination, source, rows);
}

// The source is a VIF payload in the MTVU ring, separate from VU data memory.
inline void Execute(const Plan& plan, std::uint8_t* memory, const std::uint8_t* source)
{
    switch (plan.format)
    {
    case 0:
        ExecuteRuns<0, false>(plan, memory, source);
        break;
    case 1:
        if (plan.unsigned_values)
            ExecuteRuns<1, true>(plan, memory, source);
        else
            ExecuteRuns<1, false>(plan, memory, source);
        break;
    case 2:
        if (plan.unsigned_values)
            ExecuteRuns<2, true>(plan, memory, source);
        else
            ExecuteRuns<2, false>(plan, memory, source);
        break;
    }
}
#endif
}
